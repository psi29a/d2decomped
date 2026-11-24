# SPDX-License-Identifier: GPL-3.0-or-later
"""When a click's own target search runs: FUN_00467880's gates, run by game.exe's own code.

    export D2_MPQ_DIR=<MPQ folder>
    uv run python search_gate.py [--mutation-check]
    uv run python search_gate.py inc | diff - ../../tests/search_gate_cases.inc

FUN_00467880(skill, target, &x, &y, flags, near) is the client's re-pick of a
click's target (FUN_004621d0). Here it runs for real, with the Skills.txt row
(FUN_00644140), the local player (FUN_00463dd0), the three searches
(FUN_00467490 near, FUN_00467660 enemy XY, FUN_004677b0 open XY: only which
one was called is read), FUN_00622d00 (the player is walking), FUN_004d9fc0
(the skill's status) and the dead / own-subtile side effects hooked. The click
flags are FUN_00462d00's: 1 the left button, 2 the right, 4 a press, 8 held,
0x10 a release, 0x20 shift, 0x40 run, 0x80 set by the callers.

The rule d2d ports (rules::skill_search_goes, components/rules/world/click_spot.hpp):
a held frame of the left button (flags 8 and 1) without shift (0x20) of a
player who walks (FUN_00622d00) runs no search at all, ahead of the status
test. Everything else is the dispatch: status not 0 none; the row's
SearchEnemyNear bit near; no target, SearchEnemyXY and `near` XY; SearchOpenXY
open; else none. `inc` writes the gate's cases (row with every bit set, status 0,
no target, near 1: a search runs exactly when the gate lets it) for
tests/test_search_gate.cpp.

Tags: V (game.exe's own run). U: the target is null or a live monster (the
dead-target branch, FUN_00466de0, is hooked and not compared).

`--mutation-check` breaks the model one defect at a time and requires each to
differ from game.exe.
"""
import argparse
import itertools
import sys

import emu

ENTRY = 0x467880
ROW, PLAYER, DEAD, GROUND, OWN_SUBTILE, WALKING, STATUS = 0x644140, 0x463DD0, 0x464820, 0x466DE0, 0x4C51E0, 0x622D00, 0x4D9FC0
NEAR, ENEMY_XY, OPEN_XY = 0x467490, 0x467660, 0x4677B0
NEAR_BIT, XY_BIT, OPEN_BIT = 0x40, 0x20, 0x80          # Skills.txt row +6
MUTATIONS = ("no-held", "no-left", "shift-ignored", "walking-ignored", "right-held-too")


def search_runs(flags, walking, mutation=None):
    """The gate, as d2d ports it: False when the held left frame of a walking player skips the search."""
    held = bool(flags & 8) or mutation == "no-held"
    left = bool(flags & 1) or mutation == "no-left" or (mutation == "right-held-too" and bool(flags & 2))
    shift = bool(flags & 0x20) and mutation != "shift-ignored"
    moving = bool(walking) or mutation == "walking-ignored"
    return not (held and left and not shift and moving)


def model(case, mutation=None):
    """Which search game.exe runs for `case`: near, xy, open or none."""
    flags, walking, status, bits, has_target, near = case
    gate = search_runs(flags, walking, mutation)
    if not gate or status != 0:
        return "none"
    if bits & NEAR_BIT:
        return "near"
    if not has_target and bits & XY_BIT and near:
        return "xy"
    if bits & OPEN_BIT:
        return "open"
    return "none"


class Rig:
    """FUN_00467880 over hooked neighbours."""

    def __init__(self):
        """Hook the neighbours; the row, the player and a monster are plain blocks."""
        self.e = machine = emu.Emu()
        self.row = machine.alloc(0x40)
        self.player = machine.alloc(0x200)
        self.target = machine.alloc(0x200)
        self.xy = machine.alloc(8)
        self.walking = self.status = 0
        self.called = []
        machine.w32(self.player, 0)                       # a player
        machine.w32(self.target, 1)                       # a monster
        machine.hook(ROW, lambda _: self.row, 0)
        machine.hook(PLAYER, lambda _: self.player, 0)
        machine.hook(DEAD, lambda _: 0, 0)
        machine.hook(GROUND, lambda _: 0, 0)
        machine.hook(OWN_SUBTILE, lambda _: 0, 1)
        machine.hook(WALKING, lambda _: self.walking, 1)
        machine.hook(STATUS, lambda _: self.status, 0)
        for name, address, words in (("near", NEAR, 4), ("xy", ENEMY_XY, 1), ("open", OPEN_XY, 1)):
            machine.hook(address, self._note(name), words)

    def _note(self, name):
        """A search's hook: note that it ran."""
        def run(_):
            self.called.append(name)
            return 0
        return run

    def run(self, case):
        """The search FUN_00467880 runs for the case."""
        flags, walking, status, bits, has_target, near = case
        machine = self.e
        machine.mu.mem_write(self.row + 6, bytes([bits, 0]))
        machine.w32(self.xy, 1000)                        # far from the player's path: no own-subtile call
        machine.w32(self.xy + 4, 1000)
        self.walking, self.status, self.called = walking, status, []
        machine.call(ENTRY, self.xy, self.xy + 4, flags, near, ecx=0x46, edx=self.target if has_target else 0)
        if len(self.called) > 1:
            raise AssertionError(f"case {case}: {self.called}")
        return self.called[0] if self.called else "none"


def cases():
    """Every flag byte x walking x status x row bits x target x near."""
    for flags, walking, status, bits, has_target, near in itertools.product(
            range(256), (0, 1), (0, 1), (0, NEAR_BIT, XY_BIT, OPEN_BIT, 0xE0), (0, 1), (0, 1)):
        yield flags, walking, status, bits, has_target, near


def check(rig, mutation=None):
    """Differences between game.exe and the model over every case."""
    return [(case, rig.run(case), model(case, mutation)) for case in cases() if rig.run(case) != model(case, mutation)]


def main():
    """Run the comparison, `inc`, or every mutation."""
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", nargs="?", default="check", choices=("check", "inc"))
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig = Rig()
    if args.mode == "inc":
        print("// Stock game.exe 1.14d, FUN_00467880's gate; generated by tools/emu/search_gate.py inc.")
        print("// click flags, the player walks (FUN_00622d00), a search ran (row bits all set, status 0, no target, near 1)")
        for flags, walking in itertools.product(range(256), (0, 1)):
            ran = rig.run((flags, walking, 0, 0xE0, 0, 1)) != "none"
            print(f"    {{ {flags:#x}, {walking}, {int(ran)} }},")
        return 0
    wrong = check(rig)
    if wrong:
        for case, got, want in wrong[:20]:
            print(f"differ {case}: game.exe {got}, model {want}")
        sys.exit(f"{len(wrong)} of the cases differ")
    total = sum(1 for _ in cases())
    print(f"ok: {total} FUN_00467880 gate cases, no difference")
    if args.mutation_check:
        for name in MUTATIONS:
            if not check(rig, name):
                sys.exit(f"mutation {name} was not caught")
            print(f"mutation {name}: failed as required")
    return 0


if __name__ == "__main__":
    sys.exit(main())
