# Council S1: the scheduled animation event

Traced on 2026-11-01 against a locally owned game.exe 1.14d (file version
1.14.3.71). This addresses the old todo that described the S1 shot as
immediate. No game rule is changed by this contribution.

## Scope and result

The original scheduler (`FUN_005539b0`) files one action event, type 0
with animation event value 2 and action index 0, on the first tick whose
animation position reaches the event frame: tick max(1, ceil(256 x 3 /
rate)), where rate is the word `FUN_00623f50` stores at unit +0x4c.
Dispatching that event through the original mode dispatcher reaches the S1
missile request.

The local MonStats rows for councilmember1, councilmember2 and
councilmember3 use code HP, AI HighPriest and MissS1
`highpriestlightning`. The local AnimData record HPS1HTH has 14 frames,
rate 200 and its only event, value 2, at frame index 3. That 200 is
**not** the rate S1 plays at: game.exe's mode row for S1 at `0x6e8b90`
(classes under 0x19a, `+0x14 x 8 + 4` = 1) makes it a moving mode for
`FUN_006214a0`, so `FUN_00623f50` stores the MonStats walk base times
velocitypercent (stat 0x43), as for WL. With the stock velocitypercent
75 (`FUN_00573cb0`) the rates are 126, 168 and 210, and the shot comes
**7, 5 and 4 ticks** after the mode starts for classes 345, 346 and 347.
The first section's fixtures write 200 at +0x4c, where the event is at
start + 4 ticks.

d2d files its shot on the same tick (`ai.hpp` `action_due_ms`:
`rules::frame_tick` on `mode_rate`), checked against game.exe over walk %
and anim % below. The old ms deadline gave the same ticks over that grid;
it differed elsewhere (at a 254 rate for this record, tick 3 against 4:
`tests/game/test_action_tick.cpp`).

## Original-code route

| Address | Evidence |
|---|---|
| `FUN_005e0490` | HighPriest AI selects mode 8 through `FUN_005ddf90`; this is not a direct missile call. |
| `FUN_005a7c20` | The mode-change path calls the mode's start callback, then schedules stationary-mode events through `FUN_005539b0`. |
| `FUN_005a78a0` | Selects the ordinary mode callback record for Council classes 345..347. Mode 8's record at `0x006e22e0` has start `0x005a75c0`, action `0x005a7670`, end `0x005a8030`. |
| `FUN_005a75c0` | Starts the mode. With no active skill entry it does not call the missile-creation path. |
| `FUN_005539b0` | Advances fixed-point animation positions and files type-0 events for markers 1, 2 and 4 through `FUN_005417d0`. |
| `FUN_005a7ba0` | Type-0 event callback, selected by `0x006e2490`; invokes the current mode record's action callback. |
| `FUN_005a7670` | The ordinary attack callback. Without an active skill and on the stationary path, calls `FUN_005a6d50`. |
| `FUN_0063e6b0` | Selects the mode's missile field. Mode 8's selector at `0x006ea958` is MonStats offset `+0x3e` (MissS1). |
| `FUN_005a6d50` | Reads that missile and requests it through `FUN_0056ecb0`; its ordinary level is MonsterSkillBonus + 1. |

The event scheduler and dispatcher supply the missing link between the
AI's S1 choice and its missile request. The full AI decision, resource
checks and missile's later behavior are outside this claim.

## Re-check on your own copy

Import your own game.exe into Ghidra as described in tools/ghidra. Read
the functions above with `DecompileAt` and the surrounding instructions
with `Disasm`. Keep the outputs local. On Windows the Ghidra launcher
is `support/analyzeHeadless.bat`; on other platforms use analyzeHeadless.

```sh
analyzeHeadless tools/ghidra/project D2Decomp -process game.exe -noanalysis -readOnly \
  -scriptPath tools/ghidra/scripts \
  -postScript DecompileAt /private/path/council.decomp.c 005e0490 005a75c0 005539b0 005a7ba0 005a7670 0063e6b0 005a6d50
```

Use a built `mpq-cat` and a private output directory to obtain your own
animation record. `-d` writes the binary file directly, avoiding Windows
stdout text-mode conversion. The data and generated Ghidra output must
not be committed or attached to a report.

```sh
mpq-cat -d /private/council-assets /path/to/MPQ-folder 'data\global\animdata.d2'
cd tools/emu
uv run python council_s1.py --exe /private/game.exe --animdata /private/council-assets/data/global/animdata.d2
```

Result: **12 scheduler/dispatch fixtures passed** (classes 345, 346 and
347, each at four different starting game ticks). The original
`FUN_005539b0` runs with the local animation record. Its event sink is
captured, then its one action is delivered manually to original
`FUN_005a7ba0`. Original mode lookup, action callback and missile-field
lookup reach a captured missile request at level 1. The scheduler
itself produces no immediate missile request.

With explicit `--exe` and `--animdata`, this checker does not require
`D2_MPQ_DIR` or a configured `d2d.cfg`: MPQ-cache initialization is deferred
until `Emu.mpq_file` is first used. The checker was also re-run with MPQ
configuration lookup forced to fail, and its 12 fixtures still passed.
Regression checks for deferred initialization and the first-read path run
with `uv run python -m unittest test_emu_cache` from `tools/emu`; they use
synthetic metadata and require no game files.

### Limits

- Fixtures supply synthetic MonStats/MonStats2 records for these three
  classes, with stationary S1, no active skill entry and distinct
  synthetic missile identifiers. The stock names and movement fields
  were checked separately in the local MPQ text tables.
- The starting-frame helper is replaced with zero. The original
  `FUN_00623b10` returns zero for an ordinary, untransformed monster;
  the fixture does not exercise its unit-conversion helper.
- The event sink is captured rather than running the real event queue.
  Delivery is manual, so queue ordering, interruption and cancellation
  are not tested.
- Difficulty lookup supplies MonsterSkillBonus 0; the missile-creation
  sink records the request and returns zero. No real projectile or
  post-spawn hook is executed.
- No full AI choice, damage or to-hit comparison is claimed.

## The built-d2d differential

`test_action_tick --council` prints the Council's S1 in d2d over walk %
25..175 and anim % -85..75 (105 rows: class, walk %, anim %, d2d's rate
and tick). `council_s1.py compare` runs game.exe's `FUN_00623f50` on each
row (mode_rates.py's rig and hooks: the stats are the row's) and then
`FUN_005539b0` with that rate on the stock HPS1HTH record, and compares
rate and tick:

```sh
set -o pipefail; build/tests/test_action_tick --council | grep '^D ' > d2d.tsv
cd tools/emu && uv run python council_s1.py compare ../../d2d.tsv
```

Result (2026-11-22): **105 rows, 0 differ**. A row with its tick changed
by one is reported (`DIFF ...`, exit 1). Chill changes velocitypercent
and other_animrate alike (`chill_pct`), so it is inside the grid. Not
covered: IAS, FRW (S1 here is a moving mode: FRW would count), the event
queue's own ordering and interruption.
