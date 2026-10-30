# Throw: base level and weapon eligibility

Traced against a locally owned game.exe 1.14d, file version 1.14.3.71,
on 2026-10-30.

## Finding

Throw (skill 2) has a stored base level independently of whether the
character's current weapon permits throwing. d2d used to give Throw level
zero without a throwable (`skill_base_level`), which conflated the level
with a separate usability check; #125 split them (see "Follow-up
implementation").

## Static evidence

- The local MPQ tables identify Throw as skill 2. The `Skill 1` column
  of CharStats lists Throw for Amazon, Sorceress, Necromancer, Paladin,
  Barbarian, Druid and Assassin. These rows were checked locally; no
  extracted game tables are included here.
- `FUN_00647ee0` looks for the character's own Attack entry. When absent,
  it adds Attack and walks ten skill identifiers at CharStats record
  `+0xae`, adding each valid identifier through `FUN_00647110`. This
  path contains no inventory or weapon-eligibility check.
- `FUN_00647110` creates an ordinary skill entry with level 1 at `+0x28`
  and item identifier -1 at `+0x34`. Its existing-entry path increments
  the stored level; it is not an unconditional reset to level 1.
- `FUN_006442a0(unit, entry, include_bonuses)` reads the stored level at
  `entry +0x28`, optionally adds bonuses for an ordinary entry, clamps
  negative values to zero, and caps the result using `FUN_00611830`.
  There is no weapon check in this getter.
- `FUN_00647640(entry, unit)` is a separate equipment gate. For skill 2,
  it uses inventory `unit +0x60`, the character's dual-wield capability
  (`FUN_006235a0`), and the weapon lookup `FUN_0063c9b0`. A selected
  weapon can qualify through `FUN_0062ba80` (Throwable) or stat 125.
  The opposite-hand path for dual-wield characters uses `FUN_00643d60`.
- `FUN_00647960` first reads the skill level with `FUN_006442a0`. A zero
  level yields status 7. The later equipment gate is `FUN_00647640`;
  failure reaches status 2. Further checks follow a successful equipment
  gate, so passing it alone does not prove the skill can be cast.
  `FUN_004d9fc0` wraps this status function on the client.

Thus the initial level and equipment eligibility should be represented
separately. The full usability chain also includes other equipment,
resource and state checks; this finding does not replace that chain.

## Reproduce the reading

From the repository root, use your own imported Ghidra project and a
private output directory. On Windows the launcher is
`support/analyzeHeadless.bat`; elsewhere use `analyzeHeadless`.
The output below must remain local.

```sh
analyzeHeadless tools/ghidra/project D2Decomp -process game.exe -noanalysis -readOnly \
  -scriptPath tools/ghidra/scripts \
  -postScript DecompileAt /private/path/throw.decomp.c 00647ee0 00647110 006442a0 00647640 00647960 004d9fc0 \
  -postScript Disasm 00647640 00647840 /private/path/throw-weapon.txt
```

Check the local MPQ tables with `mpq-cat <MPQ-folder>
'data\global\excel\charstats.txt'` and the corresponding `skills.txt`.
Inspect them locally; do not attach their full contents to a report.

## Isolated original-code checks

The harness below runs the original equipment gate and base-level getter
in Unicorn in 35 contexts: seven player classes, each with no inventory,
no matching weapon, an ineligible weapon, a Throwable-classified weapon,
and a weapon qualifying through stat 125. The skill entry is supplied
with a stored level of 1; this test does not itself establish how a new
player obtains the entry.

Inventory lookup and item classification/stat helpers are replaced with
controlled inputs. The weapon gate and level getter are original code.
The level cap is supplied as 60, and bonuses are disabled. The fixture
uses a selected weapon; the dual-wield opposite-hand branch is not tested.

Save the following harness locally. From `tools/emu`, set `D2_GAME_EXE`
to your own game.exe and run `uv run python /private/path/check_throw.py`.

```python
import os
import sys
from pathlib import Path
sys.path.insert(0, str(Path.cwd()))  # run from tools/emu
from emu import Emu

oracle = Emu(exe=os.environ["D2_GAME_EXE"])
unit, skill, row, inventory, item, cap = [
    oracle.alloc(size) for size in (0x200, 0x40, 0x240, 0x100, 0x100, 28)
]
oracle.w32(skill, row)
oracle.w32(row, 2)
oracle.w32(skill + 0x28, 1)
oracle.w32(skill + 0x34, -1)
oracle.w32(cap, 60)
oracle.w32(0x96c8a8, cap)
state = dict(found=1, throwable=1, stat125=0)

def inventory_lookup(machine):
    machine.w32(machine.arg(1), item)
    machine.w32(machine.arg(2), 4)
    machine.w32(machine.arg(3), 1)  # selected weapon
    return state["found"]

oracle.hook(0x63c9b0, inventory_lookup, 4)
oracle.hook(0x62ba80, lambda machine: state["throwable"], 1)
oracle.hook(0x625500, lambda machine: state["stat125"], 3)
cases = 0
for character_class in range(7):
    oracle.w32(unit + 4, character_class)
    for has_inventory, found, throwable, stat125, expected in [
        (0, 0, 0, 0, False), (1, 0, 0, 0, False),
        (1, 1, 0, 0, False), (1, 1, 1, 0, True),
        (1, 1, 0, 1, True),
    ]:
        oracle.w32(unit + 0x60, inventory if has_inventory else 0)
        state.update(found=found, throwable=throwable, stat125=stat125)
        result = oracle.call(0x647640, ecx=skill, edx=unit, limit=10000)
        assert bool(result) == expected, (character_class, state, result)
        level = oracle.call(0x6442a0, unit, skill, 0, limit=10000)
        assert level == 1, (character_class, state, level)
        cases += 1
print(f"PASS: {cases} weapon contexts; stored base level remains 1")
```

Result: 35 contexts passed locally. Player initialization was read
statically, not exercised end to end in the emulator. Real inventory
resolution, depleted stacks, weapon swapping, full UI behavior, the
complete usability-status chain and a C++ comparison remain untested.
There has not yet been an independent maintainer re-run. This is a trace
with an isolated oracle check, not a claim of full d2d parity.

## Follow-up implementation

Done in #125. Throw stays at level 1 (`skill_base_level`), and the weapon
is `throw_ready` (`components/game/fight.hpp`): the right hand's, else the
left's, usable weapon (`FUN_0063c9b0`, `FUN_0062a4e0` flags 0x100 /
0x4000) is Throwable or has stat 125. Without one the skill is refused
(`skill_refused`): the picker still lists it and the buttons show it, both
in text colour 1 (`FUN_004aa3a0`, `FUN_004a8d30`), and a cast stops with
the class's `impossible_1` (`FUN_004610c0`). The picker, cast and voice
paths are in skills.md "Item charges"; `test_game` covers the gate.

Still open (`ponytail:` in `throw_ready` / `skill_refused`): a
dual-wielder with two weapons uses the primary one (`FUN_0063bef0`), else
the other hand (`FUN_00643d60`), where d2d takes either; and the other
statuses of `FUN_00647960`.
