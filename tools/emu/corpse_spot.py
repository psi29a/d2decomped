# SPDX-License-Identifier: GPL-3.0-or-later
"""Where a saved corpse is laid, as the oracle: FUN_005352c0's spot search.

    uv run python corpse_spot.py gen 20000 1 > cases.tsv
    uv run python corpse_spot.py game cases.tsv > g.txt
    ../../build/tests/test_corpse_spot --corpse-spot cases.tsv g.txt   # d2d's corpse_spot: 0 differ

When a player with a saved corpse comes into the game (FUN_005394a0), the
corpse starts on the act town's spawn-index-0 spot, the one the player has
just been placed on, and FUN_005352c0 moves it to the nearest free spot:
FUN_0064e7e0(room, &xy, pattern 2, mask 0x9c19, 4) -> FUN_0064dea0 (50
rings). The player stands there first, so its footprint 0x1000 is on the
start subtile (FUN_0064ea90 pattern 1); 0x9c19 holds 0x1000, so the corpse
(a 3 x 3 test, pattern 2) ends next to the player, not on them.

A case is `id sx sy n {x y bits}...`: the start subtile, then the non-zero
collision words (walls, doors, objects; no units). `game` stamps 0x1000 on
the start, runs FUN_0064e7e0 and prints `id found x y` (found: its return,
0 or 1; x y the spot it leaves). The world and the hooks are sk_tele_spot's
(5 x 5 rooms of 40 x 40, a room's near list its touching rooms); run for
real: FUN_0064e7e0, FUN_0064dea0 and the footprint test under it.

What the run shows (a Python model matched 20000 of 20000, seed 1):
FUN_0064e7e0 passes FUN_0064dea0 a step of 4 (its last argument; FUN_0064e7b0
passes 1), and pattern 2 goes to FUN_0064d9b0, which tests the plus (the
subtile and its four neighbours) against 0x9c19. The start itself if clear;
else ring k = 1..49 at radius r = 1 + 4 (k - 1): its columns x - r and x + r
for y - r, y - r + 4, ... <= y + r (left then right a row), then its rows
y - r and y + r for x - r + 1, x - r + 5, ... <= x + r - 1 (top then bottom
a column); in the first ring with a clear spot the least |dx| + |dy| wins,
a tie the first scanned. With the player on the start that's (x - 1, y - 1)
unless something blocks it.
"""
import random
import sys

import sk_tele_spot as tele

PLAYER = 0x1000


def gen(count, seed):
    rng = random.Random(seed)
    for cid in range(count):
        _, _, _, _, _, words = tele.make_case(rng, False)
        sx, sy = rng.randrange(2 * tele.ROOM, 3 * tele.ROOM), rng.randrange(2 * tele.ROOM, 3 * tele.ROOM)
        if rng.random() < 0.3:                                  # a crowded start: walls right round it
            for _ in range(rng.randint(3, 20)):
                at = (sx + rng.randint(-3, 3), sy + rng.randint(-3, 3))
                words[at] = words.get(at, 0) | rng.choice((0x1, 0x400, 0x8000, 0x800))
        words.pop((sx, sy), None)
        flat = " ".join(f"{x} {y} {value}" for (x, y), value in sorted(words.items()))
        print(f"{cid} {sx} {sy} {len(words)} {flat}".rstrip())


def game(path):
    oracle = tele.Oracle()
    e = oracle.e
    xy = e.alloc(8)
    for line in open(path):
        field = list(map(int, line.split()))
        cid, sx, sy, count = field[:4]
        words = {(field[4 + 3 * k], field[5 + 3 * k]): field[6 + 3 * k] for k in range(count)}
        words[(sx, sy)] = words.get((sx, sy), 0) | PLAYER
        oracle.load(words)
        e.w32(xy, sx)
        e.w32(xy + 4, sy)
        room = oracle.rooms[(sx // tele.ROOM, sy // tele.ROOM)]
        found = e.call(0x64E7E0, room, xy, 2, 0x9C19, 4) & 0xFFFFFFFF
        print(cid, int(found != 0), e.s32(xy), e.s32(xy + 4))


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]))
    elif len(sys.argv) == 3 and sys.argv[1] == "game":
        game(sys.argv[2])
    else:
        sys.exit(__doc__)
