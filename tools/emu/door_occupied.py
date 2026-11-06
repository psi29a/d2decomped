# SPDX-License-Identifier: GPL-3.0-or-later
"""Is a unit in a door's doorway, as the oracle: FUN_0064ea90 then FUN_0064d800.

    uv run python door_occupied.py 100000 1 > g.txt
    ../../build/tests/test_doorway --doorway g.txt   # d2d's rules::in_doorway: 0 differ

A door (FUN_00581d40, modes 2 / 5) asks FUN_0064d800(room, x, y, SizeX,
SizeY, 0x8180) over its rect: is any subtile there marked with a unit's own
bit (player 0x80, monster 0x100)? A unit marks them by its collision pattern
(FUN_0064ea90(room, x, y, pattern, bits)). Both run for real, on one fake
room (origin RX, RY, 60 x 60 subtiles; room +0x4c.. its rect, +0x20 the
collision record {+0 x, +4 y, +8 width, +0x20 words}) cleared each case.

A line a case:

    PATTERN BITS UX UY DX DY SX SY OCCUPIED

What the run shows: pattern 0 marks nothing; 1 / 3 / 5 the plus (the
subtile and its four neighbours, FUN_0064dc80); 2 / 4 the 3 x 3 square round
the subtile (FUN_0064ddf0: x-1..x+1, y-1..y+1), whatever the unit's SizeX.
The rect is DX - SX/2 .. DX - SX/2 + SX - 1 (same for y); SX and SY both 1
or less tests DX, DY alone (FUN_0064ca50). Sizes run 1..9: every door row
in Objects.txt (IsDoor or OperateFn 8) is 1 x 3, 3 x 1, 1 x 4, 4 x 1, 1 x 7,
7 x 1 or 9 x 1; a 0 against a size over 1 is a rect game.exe reads oddly
(FUN_0064ceb0), not a case a door makes.
"""
import random
import sys

import emu

SIZE = 60


def main(count, seed):
    rng = random.Random(seed)
    e = emu.Emu()
    words = e.alloc(SIZE * SIZE * 2)
    coll = e.alloc(0x40)
    room = e.alloc(0x100)
    e.w32(room + 0x20, coll)
    for offset in (0x4C, 0x50, 0x54, 0x58):
        e.w32(room + offset, 0)
    e.w32(coll + 8, SIZE)
    e.w32(coll + 0x20, words)
    blank = bytes(SIZE * SIZE * 2)
    for _ in range(count):
        origin_x, origin_y = rng.randrange(0, 2000, 5), rng.randrange(0, 2000, 5)
        for offset, value in ((0x4C, origin_x), (0x50, origin_y), (0x54, SIZE), (0x58, SIZE)):
            e.w32(room + offset, value)
        e.w32(coll + 0, origin_x)
        e.w32(coll + 4, origin_y)
        e.w32(coll + 0xC, SIZE)
        e.mu.mem_write(words, blank)
        pattern, bits = rng.randrange(6), rng.choice((0x80, 0x100))
        door_x, door_y = origin_x + rng.randrange(20, 40), origin_y + rng.randrange(20, 40)
        size_x, size_y = rng.choice((1, 1, 2, 3, 4, 5, 7, 9)), rng.choice((1, 1, 2, 3, 4, 5, 7, 9))
        unit_x, unit_y = door_x + rng.randrange(-6, 7), door_y + rng.randrange(-6, 7)
        e.call(0x64EA90, room, unit_x, unit_y, pattern, bits)
        occupied = e.call(0x64D800, room, door_x, door_y, size_x, size_y, 0x8180) & 0xFFFF
        print(pattern, bits, unit_x - origin_x, unit_y - origin_y, door_x - origin_x, door_y - origin_y,
              size_x, size_y, int(occupied != 0))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(int(sys.argv[1]), int(sys.argv[2]))
