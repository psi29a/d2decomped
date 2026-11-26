# SPDX-License-Identifier: GPL-3.0-or-later
"""Every room's vis slot flags (room2 +0x28 & 0xff0), game.exe's against d2d's.

    uv run python room_vis.py [FIRST-LAST] [--mutation-check]     # map seeds, default 1-3

game.exe: each level of LEVELS (default the Blood Moor, Cold Plains, the
Monastery Gate, the Outer Cloister, the Rocky Waste, the Lut Gholein
Sewers' first level, the Frigid Highlands) built from the map seed (FUN_006194a0) and every room
read before it comes up: 0x10 << n for each Vis slot n FUN_00667970 OR'd
into a preset's cells (a walk-across slot: Vis set, Warp -1; a slot wall
of its DS1) and a plain room's cell +0x18 (FUN_006750f0 -> FUN_0067d540:
the contact slots of FUN_00675770). d2d: `test_room_near --near SEED
LEVEL...` with NEAR_VIS set (Level::room_flags). Prints `ok: N levels'
room vis flags match` when all agree. `--mutation-check` also demands
that d2d's flags with the walk-across slots cleared differ from game.exe's,
so a check blind to those slots would be seen.
Needs $D2_BUILD (tests/test_room_near, mpq-cat), $D2_MPQ_DIR, $D2_PATCH_INSTALLER.
"""
import os
import re
import subprocess
import sys

import drlg
from near_rooms import act_of

LEVELS = [int(level) for level in os.environ.get("LEVELS", "2,3,26,27,41,47,111").split(",")]


def game_vis(e, seed, level_id):
    """game.exe's rooms of `level_id`: {(x, y, w, h) level-relative: +0x28 & 0xff0}, as generated."""
    act = drlg.alloc_act(e, act_of(level_id), seed, level_id)
    lvl = drlg.find_level(e, act, level_id)
    if not lvl:
        return {}
    x0, y0 = e.s32(lvl + 0x1c), e.s32(lvl + 0x20)
    rooms = {}
    room = e.r32(lvl + 0x10)
    while room:
        x, y, w, h = (e.s32(room + 0x34 + 4 * i) for i in range(4))
        rooms[(x - x0, y - y0, w, h)] = e.r32(room + 0x28) & 0xff0
        room = e.r32(room + 0x24)
    return rooms


def our_vis(seed):
    """d2d's, per level: test_room_near's `room x,y wxh vis V:` lines."""
    test = os.path.join(os.environ["D2_BUILD"], "tests", "test_room_near")
    out = subprocess.run([test, "--near", hex(seed)] + [str(level) for level in LEVELS], capture_output=True,
                         text=True, env={**os.environ, "NEAR_VIS": "1"}, check=True).stdout
    levels, current = {}, None
    for line in out.splitlines():
        if level := re.match(r"level (\d+) rooms", line):
            current = levels.setdefault(int(level[1]), {})
        elif room := re.match(r"room (-?\d+),(-?\d+) (\d+)x(\d+) vis ([0-9a-f]+):", line):
            current[tuple(int(room[i]) for i in range(1, 5))] = int(room[5], 16)
    return levels


def main():
    span = next((arg for arg in sys.argv[1:] if not arg.startswith("--")), "1-3")
    first, last = (int(part, 0) for part in span.split("-"))
    mutation = "--mutation-check" in sys.argv
    e = drlg.boot()
    failures, checked, mutants_seen = 0, 0, 0
    for seed in range(first, last + 1):
        ours = our_vis(seed)
        for level_id in LEVELS:
            game = game_vis(e, seed, level_id)
            mine = ours.get(level_id, {})
            checked += 1
            if game != mine:
                failures += 1
                for rect in sorted(set(game) | set(mine)):
                    if game.get(rect) != mine.get(rect):
                        print(f"seed {seed} level {level_id} room {rect}: game.exe {game.get(rect, '-')!s:>4} d2d {mine.get(rect, '-')}")
                continue
            if mutation:
                # Clear what every room with flags shares (the walk-across slots of a preset level):
                # game.exe's must then differ wherever those bits stood.
                shared = 0xff0
                for value in game.values():
                    if value:
                        shared &= value
                mutant = {rect: value & ~shared for rect, value in mine.items()}
                if shared and mutant != game:
                    mutants_seen += 1
    if failures:
        print(f"FAIL: {failures} of {checked} level dumps differ")
        sys.exit(1)
    if mutation and not mutants_seen:
        print("FAIL: --mutation-check: no level's walk-across slots were seen")
        sys.exit(1)
    extra = f", the walk-slot mutant caught on {mutants_seen}" if mutation else ""
    print(f"ok: {checked} levels' room vis flags match{extra}")


if __name__ == "__main__":
    main()
