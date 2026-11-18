# SPDX-License-Identifier: GPL-3.0-or-later
"""Check MonUMod 14 (spcdamage), FUN_005a3b50 (hook 0), in stock game.exe 1.14d.

Run: export D2_MPQ_DIR=/home/gui/Projects/d2decomped/dev/data
     uv run python spcdamage.py [--mutation-check]
     uv run python spcdamage.py inc --out ../../tests/spcdamage_cases.inc

FUN_005a3b50 (ECX the game, EDX a monster) runs game.exe's own code with its
callees hooked (FUN_00620bb0 the unit's room, null for the no-room case,
FUN_0061a1b0 the room's level id, 0 for a null room as game.exe's own
`test eax, eax; jnz` leaves it, FUN_0061dca0 the area's monster level, the
stat setters FUN_00627260 / FUN_00627150 recording, FUN_00625760 the stat
list). The claim, over every BaseId 0x140..0x175 and area levels from -3 to
120, and a unit with no room (a null room, so level id 0):
  - level (stat 12) = the area level, 1 under 2, or 2 with no room;
  - to-hit (stat 0x13) = level + 50, at most 90;
  - BaseId 0x146, 0x147 / 0x148, 0x149, 0x14a / 0x171 add the stat pairs of
    components/rules/uniques.hpp's rules::spcdamage, in that order.
`inc` prints the game's answers for tests/monsters/test_spcdamage.cpp to stdout
(diagnostics go to stderr); `inc --out FILE` writes them to FILE only once the
whole run has passed, so a failed run leaves the checked-in fixture alone.
No MPQs are read.
"""
import argparse
import hashlib
import sys
from pathlib import Path

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
TARGET = 0x5A3B50
NO_AREA = -1000
MUT = set()
AREAS = [NO_AREA, -3, 0, 1, 2, 3, 4, 5, 8, 9, 20, 39, 40, 41, 55, 79, 80, 85, 99, 120]


def model(base, area):
    """rules::spcdamage re-stated: (level, to_hit, [(stat, value)])."""
    level = 2 if area == NO_AREA else (1 if area < 2 else area)
    if "floor" in MUT and area != NO_AREA:
        level = area
    to_hit = min(level + 50, 90 if "cap" not in MUT else 91)
    half = level >> 1
    out = []
    if base == 0x146:
        out = [(0x15, 0), (0x16, 0), (0x30, half), (0x31, level * 3 >> 1)]
    elif base in (0x147, 0x148):
        out = [(0x15, min(half + 1, level)), (0x16, level)]
    elif base == 0x149:
        out = [(0x15, 0), (0x16, 0), (0x39, level), (0x3A, level * 2), (0x3B, level * 2 if "poison" not in MUT else level * 3)]
    elif base in (0x14A, 0x171):
        out = [(0x15, 0), (0x16, 0), (0x32, half), (0x33, level * 3 >> 1)]
    return level, to_hit, out


def signed(value):
    """A 32-bit register value as a signed int."""
    return value - (1 << 32) if value >> 31 else value


class Game:
    """game.exe with FUN_005a3b50's callees hooked once; `case` runs one monster and area."""

    def __init__(self, exe):
        """Load the exe and hook the callees (they read self.area and fill self.seen)."""
        self.machine = machine = emu.Emu(str(exe))
        self.area = 0
        self.seen = {}
        self.game = machine.alloc(0x200)
        self.unit = machine.alloc(0x200)
        machine.mu.mem_write(self.game + 0x6D, bytes([2]))
        machine.w32(self.game + 0x70, 1)
        machine.w32(self.unit, 1)
        machine.hook(0x620BB0, lambda e: 0 if self.area == NO_AREA else 0x1000, 1)
        machine.hook(0x61A1B0, self.room_level, 1)
        machine.hook(0x61DCA0, self.area_level, 3)
        machine.hook(0x625760, lambda e: 0x2000, 2)
        machine.hook(0x627260, self.set_stat, 4)
        machine.hook(0x627150, self.list_stat, 4)

    def room_level(self, current):
        """Hook FUN_0061a1b0: record the room asked about; a null room gives level id 0 (game.exe's own early return)."""
        self.seen["room"] = current.arg(0)
        return 0 if current.arg(0) == 0 else 7

    def area_level(self, current):
        """Hook FUN_0061dca0: record (level id, difficulty, expansion) and answer the area level."""
        self.seen["level_args"] = (current.arg(0), current.arg(1) & 0xFF, current.arg(2))
        return self.area & 0xFFFFFFFF

    def set_stat(self, current):
        """Hook FUN_00627260: a stat put on the unit itself."""
        self.seen["unit"][current.arg(1)] = signed(current.arg(2))
        return 0

    def list_stat(self, current):
        """Hook FUN_00627150: a stat put on the unit's stat list."""
        assert current.arg(0) == 0x2000
        self.seen["list"].append((current.arg(1), signed(current.arg(2))))
        return 0

    def case(self, base, area):
        """Run FUN_005a3b50 for one monster class and area; return (level, to_hit, pairs)."""
        self.area = area
        self.seen = {"unit": {}, "list": [], "level_args": None}
        self.machine.w32(self.unit + 4, base)
        self.machine.call(TARGET, 0, 0, ecx=self.game, edx=self.unit, limit=5000)
        if area == NO_AREA:
            assert self.seen["room"] == 0 and self.seen["level_args"] is None
        else:
            assert self.seen["room"] == 0x1000 and self.seen["level_args"] == (7, 2, 1), self.seen["level_args"]
        return self.seen["unit"][12], self.seen["unit"][0x13], self.seen["list"]


def run(mutation):
    """Compare game.exe to the model for every case; return (bad, cases, rows)."""
    MUT.clear()
    MUT.update(mutation)
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    bad = cases = 0
    rows = []
    game = Game(exe)
    for base in range(0x140, 0x176):
        for area in AREAS:
            got = game.case(base, area)
            want = model(base, area)
            cases += 1
            rows.append((base, area, got))
            if got != want:
                bad += 1
                print(f"base {base:#x} area {area}: game {got} model {want}", file=sys.stderr)
    return bad, cases, rows


def main():
    """Run the oracle; `inc` prints the C++ fixture; --mutation-check proves the mutants are caught."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", default="check", choices=("check", "inc"))
    parser.add_argument("--mutation-check", action="store_true")
    parser.add_argument("--out", help="with inc: write the fixture here, after the run has passed")
    args = parser.parse_args()
    if args.mode == "inc" and args.mutation_check:
        parser.error("inc and --mutation-check are separate runs")
    if args.out and args.mode != "inc":
        parser.error("--out goes with inc")
    bad, cases, rows = run(())
    if bad:
        print(f"FAIL: {bad} of {cases} differ", file=sys.stderr)
        sys.exit(1)
    if args.mode == "inc":
        lines = ["// Stock game.exe 1.14d, FUN_005a3b50; generated by tools/emu/spcdamage.py inc.",
                 "// BaseId, area level (-1000: no room), level, to-hit, pair count, up to 5 (stat, value) pairs"]
        for base, area, (level, to_hit, pairs) in rows:
            padded = pairs + [(0, 0)] * (5 - len(pairs))
            text = ", ".join(f"{{ {stat}, {value} }}" for stat, value in padded)
            lines.append(f"    {{ {base}, {area}, {level}, {to_hit}, {len(pairs)}, {{ {text} }} }},")
        if args.out:
            temporary = Path(args.out).with_name(Path(args.out).name + ".tmp")
            temporary.write_text("\n".join(lines) + "\n")
            temporary.replace(args.out)
        else:
            print("\n".join(lines))
        return
    print(f"ok: {cases} FUN_005a3b50 cases (BaseId 0x140..0x175 x {len(AREAS)} areas)")
    if args.mutation_check:
        for name in ("floor", "cap", "poison"):
            bad, _, _ = run((name,))
            print(f"mutant {name}: {'caught' if bad else 'NOT CAUGHT'} ({bad})")
            if not bad:
                sys.exit(1)
        print("mutation check ok")


if __name__ == "__main__":
    main()
