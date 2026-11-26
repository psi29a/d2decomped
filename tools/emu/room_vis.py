# SPDX-License-Identifier: GPL-3.0-or-later
"""Every room's vis slot flags (room2 +0x28 & 0xff0), game.exe's against d2d's.

    NEAR_VIS=1 $D2_BUILD/tests/test_room_near --near SEED LEVEL... | uv run python room_vis.py SEED [--mutation-check]

game.exe: each level d2d's dump on stdin holds, built from the map seed
(FUN_006194a0), every room read before it comes up: 0x10 << n for each
Vis slot n FUN_00667970 OR'd into a preset's cells (a walk-across slot:
Vis set, Warp -1; a slot wall of its DS1) and a plain room's cell +0x18
(FUN_006750f0 -> FUN_0067d540: the contact slots of FUN_00675770). d2d:
`test_room_near --near SEED LEVEL...` with NEAR_VIS set
(Level::room_flags), its `room x,y wxh vis V:` lines. Prints `ok: N
levels' room vis flags match` when all agree. `--mutation-check` also
demands that d2d's flags with the walk-across slots cleared differ from
game.exe's, so a check blind to those slots would be seen.
"""
import re
import sys

import drlg
from near_rooms import act_of


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


def our_vis(text):
    """d2d's, per level: test_room_near's `level N rooms` and `room x,y wxh vis V:` lines in `text`."""
    levels, current = {}, None
    for line in text.splitlines():
        if level := re.match(r"level (\d+) rooms", line):
            current = levels.setdefault(int(level[1]), {})
        elif (room := re.match(r"room (-?\d+),(-?\d+) (\d+)x(\d+) vis ([0-9a-f]+):", line)) and current is not None:
            current[tuple(int(room[i]) for i in range(1, 5))] = int(room[5], 16)
    return levels


def main():
    """Diff game.exe's flags for the seed on the command line against d2d's dump on stdin."""
    args = [arg for arg in sys.argv[1:] if not arg.startswith("--")]
    if not args:
        sys.exit("usage: ... | room_vis.py SEED [--mutation-check]")
    seed = int(args[0], 0)
    mutation = "--mutation-check" in sys.argv
    ours = our_vis(sys.stdin.read())
    if not ours:
        sys.exit("FAIL: no `level` lines on stdin (NEAR_VIS=1 test_room_near --near SEED LEVEL...)")
    e = drlg.boot()
    failures, mutants_seen = 0, 0
    for level_id, mine in sorted(ours.items()):
        game = game_vis(e, seed, level_id)
        if game != mine:
            failures += 1
            for rect in sorted(set(game) | set(mine)):
                if game.get(rect) != mine.get(rect):
                    print(f"seed {seed} level {level_id} room {rect}: game.exe {game.get(rect, '-')} d2d {mine.get(rect, '-')}")
            continue
        if mutation:
            # Clear what every room with flags shares (the walk-across slots of a preset level):
            # game.exe's must then differ wherever those bits stood.
            shared = 0xff0
            for value in game.values():
                if value:
                    shared &= value
            if shared and {rect: value & ~shared for rect, value in mine.items()} != game:
                mutants_seen += 1
    if failures:
        print(f"FAIL: {failures} of {len(ours)} level dumps differ")
        sys.exit(1)
    if mutation and not mutants_seen:
        print("FAIL: --mutation-check: no level's walk-across slots were seen")
        sys.exit(1)
    extra = f", the walk-slot mutant caught on {mutants_seen}" if mutation else ""
    print(f"ok: {len(ours)} levels' room vis flags match{extra}")

if __name__ == "__main__":
    main()
