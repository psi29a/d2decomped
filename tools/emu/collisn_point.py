# SPDX-License-Identifier: GPL-3.0-or-later
"""The point collision dispatch and a walker's blocked-move test, as the oracle:
FUN_0064d870 (CheckCollision_BlockAll_Type) and FUN_0064eda0 (RemoveGetCollision_Type).

    uv run python collisn_point.py 2000 1 > g.txt
    awk '{print $1}' g.txt | sort | uniq -c   # every pattern's dispatch agreed with its own shape function

docs/research/re/collision.md "Point tests by collision pattern" (FUN_0064d870) and
"A walk's stop" (FUN_0064eda0, called through FUN_0064ff90): a walker tests a new
subtile by removing its own stamp (FUN_0064ec10), checking the target subtile against
its mask, and putting the stamp back where it failed (or forward where it didn't).

Part 1 (one line `dispatch PATTERN OK`): FUN_0064d870 is only a jump table. For each
pattern 0..5 it's run against a random room and mask, then diffed against calling the
same pattern's own shape function directly (0 -> FUN_0064d450, 1/3/5 -> FUN_0064d100
the plus, 2/4 -> FUN_0064d4a0 the 3x3 box): the same two real runs of game.exe, which
must agree if the table is read right. A pattern over 5 (no door uses one) hits the
switch's default and returns COLLISION_ALL unconditionally, line `default FLAGS`.

Part 2 (one line `move RESULT`): a unit's own stamp (pattern 1, COLBIT_PLAYER 0x80) sits
at (ux, uy). FUN_0064eda0 is asked to move it to (cx, cy) against the player's real mask
from collision.md's "The grid" (0x1c09), with the checkpoint either clear or pre-blocked.
Then FUN_0064d870 reads both points again, to show which one now holds the stamp: RESULT
0 moves it (old point clear, new point has it); RESULT nonzero (the blocking flags)
restores it at the old point and leaves the checkpoint as it was.

Room and grid are the same fake layout as door_occupied.py's (origin RX, RY, 21 x 21
subtiles, room +0x20 -> collision record {+0 x, +4 y, +8 width, +0xc height, +0x20
words}), kept well clear of the room's edge so the plus and 3x3 box never reach outside
it (CheckCollision_Cross's adjacent-room lookup is not exercised here).
"""
import random
import sys

import emu

SIZE = 21
CENTER = SIZE // 2
PLAYER_MASK = 0x1C09   # collision.md "The grid": a player's own mask


def make_room(e, origin_x, origin_y):
    """D2RoomStrc (room, structs: D2RoomStrc) has its own sCoords at +0x4c
    (DRLGROOM_FindBetterNearbyRoom reads that copy, not the grid's), and
    pCollisionGrid at +0x20 (D2RoomCollisionGridStrc, structs: D2RoomCollisionGridStrc):
    its own sCoords again at +0, pMapStart at +0x20. Both copies must agree,
    or DRLGROOM_FindBetterNearbyRoom decides (x, y) isn't in this room at all
    and every check falls back to "no room", returning the mask unchanged."""
    words = e.alloc(SIZE * SIZE * 2)
    coll = e.alloc(0x40)
    room = e.alloc(0x100)
    e.w32(room + 0x20, coll)
    e.w32(coll + 0x20, words)
    for base in (room + 0x4C, coll):
        e.w32(base + 0, origin_x)
        e.w32(base + 4, origin_y)
        e.w32(base + 8, SIZE)
        e.w32(base + 0xC, SIZE)
    return room, coll, words


def set_cell(e, coll, words, origin_x, origin_y, x, y, value):
    e.mu.mem_write(words + ((y - origin_y) * SIZE + (x - origin_x)) * 2, (value & 0xFFFF).to_bytes(2, "little"))


def dispatch_case(e, room, coll, words, rng, origin_x, origin_y):
    e.mu.mem_write(words, bytes(SIZE * SIZE * 2))
    x, y = origin_x + CENTER, origin_y + CENTER
    mask = rng.randrange(1, 0x10000)
    for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)):
        set_cell(e, coll, words, origin_x, origin_y, x + dx, y + dy, rng.randrange(0x10000))
    pattern = rng.randrange(7)
    actual = e.call(0x64D870, room, x, y, pattern, mask) & 0xFFFF
    if pattern == 0:
        expected = e.call(0x64D450, mask, ecx=room, regs={"EDI": x, "EAX": y}) & 0xFFFF
    elif pattern in (1, 3, 5):
        expected = e.call(0x64D100, room, y, regs={"EAX": x, "EBX": mask}) & 0xFFFF
    elif pattern in (2, 4):
        expected = e.call(0x64D4A0, y, mask, ecx=room, regs={"EAX": x}) & 0xFFFF
    else:
        expected = None
    if expected is not None:
        print("dispatch", pattern, "OK" if actual == expected else f"DISAGREE actual={actual:#x} expected={expected:#x}")
    else:
        print("default", hex(actual))


def move_case(e, room, coll, words, rng, origin_x, origin_y):
    e.mu.mem_write(words, bytes(SIZE * SIZE * 2))
    ux, uy = origin_x + CENTER, origin_y + CENTER
    cx, cy = ux + rng.choice((-2, 2)), uy + rng.choice((-2, 2))
    e.call(0x64EA90, room, ux, uy, 1, 0x80)   # own stamp: pattern 1 (the plus), COLBIT_PLAYER
    blocked = rng.random() < 0.5
    if blocked:
        set_cell(e, coll, words, origin_x, origin_y, cx, cy, 8)   # COLBIT_NOPLAYER: inside the player's 0x1c09 mask
    result = e.call(0x64EDA0, uy, cx, cy, 1, 0x80, PLAYER_MASK, ecx=room, edx=ux) & 0xFFFF
    at_old = e.call(0x64D870, room, ux, uy, 1, 0x80) & 0xFFFF
    at_new = e.call(0x64D870, room, cx, cy, 1, 0x80) & 0xFFFF
    ok = (result == 0 and at_old == 0 and at_new == 0x80) if not blocked else (result != 0 and at_old == 0x80)
    print("move", hex(result), "blocked" if blocked else "free", "OK" if ok else "DISAGREE", at_old, at_new)


def main(count, seed):
    rng = random.Random(seed)
    e = emu.Emu()
    origin_x, origin_y = 1000, 1000
    room, coll, words = make_room(e, origin_x, origin_y)
    for _ in range(count):
        dispatch_case(e, room, coll, words, rng, origin_x, origin_y)
        move_case(e, room, coll, words, rng, origin_x, origin_y)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(int(sys.argv[1]), int(sys.argv[2]))
