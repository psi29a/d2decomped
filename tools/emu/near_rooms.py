# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's room near lists (FUN_0066c370), the oracle for d2d's near_list.

    uv run python near_rooms.py <map seed> <level> [<level> ...]   # prints what `test_room_near --near` prints

FUN_0061b730 brings each room of the level up (the act made by drlg.alloc_act,
as tools/emu/drlg.py does), which builds its near list: room2 +8 the array,
+0x2c the count (FUN_0066bda0), each entry a room2 whose +0x58 is its level.
Each room: its level-relative tile rect, then each near room as level id and
rect relative to the asked level's origin, sorted, so the list's order
(FUN_0066bbc0's sort) doesn't take part. With NEAR_VIS set, each room's vis
flags (+0x28 & 0xff0, FUN_00667970's bits) too, as `test_room_near --near`
prints them with it set.
"""
import os
import sys

import drlg

ACT_OF = [(1, 39, 0), (40, 74, 1), (75, 102, 2), (103, 108, 3), (109, 136, 4)]


def act_of(level_id):
    return next(act for first, last, act in ACT_OF if first <= level_id <= last)


def main():
    seed = int(sys.argv[1], 0)
    wanted = [int(arg) for arg in sys.argv[2:]]
    e = drlg.boot()
    for level_id in wanted:
        act = drlg.alloc_act(e, act_of(level_id), seed, level_id)
        lvl = drlg.find_level(e, act, level_id)
        if not lvl:
            print(f"level {level_id}: none")
            continue
        x0, y0 = e.s32(lvl + 0x1c), e.s32(lvl + 0x20)
        rooms = []
        r = e.r32(lvl + 0x10)
        while r:
            rooms.append(r)
            r = e.r32(r + 0x24)
        for r in rooms:
            e.call(0x61b730, ecx=r)
        print(f"level {level_id} rooms {len(rooms)}")
        rect = lambda room: tuple(e.s32(room + 0x34 + 4 * i) for i in range(4))
        for r in sorted(rooms, key=lambda room: (rect(room)[1], rect(room)[0])):
            rx, ry, rw, rh = rect(r)
            near = []
            array, count = e.r32(r + 8), e.r32(r + 0x2c)
            for k in range(count):
                other = e.r32(array + 4 * k)
                ox, oy, ow, oh = rect(other)
                near.append((e.r32(e.r32(other + 0x58) + 0x1d0), ox - x0, oy - y0, ow, oh))
            vis = f" vis {e.r32(r + 0x28) & 0xff0:x}" if os.environ.get("NEAR_VIS") else ""
            print(f"room {rx - x0},{ry - y0} {rw}x{rh}{vis}: "
                  + " ".join(f"{lid}@{ox},{oy},{ow}x{oh}" for lid, ox, oy, ow, oh in sorted(near)))


if __name__ == "__main__":
    main()
