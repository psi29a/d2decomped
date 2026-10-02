# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's monster regions for a game seed: FUN_00547d20's step, then FUN_005479c0.

    uv run python regions.py <map seed> [difficulty] [expansion]   # one line per level with monsters

With a fixed seed (-seed) the game seed is {map seed, 666} (FUN_0052c280).
Region (0x2e4 bytes): +0x10 type count, +0x14 types (0x34 each: +0 MonStats
row, +2 rarity, +3 component sets, +4 three sets of 16 components).
"""
import sys

import drlg


def regions(e, seed, difficulty=0, expansion=1):
    state = (seed * 0x6ac690c5 + 666) & 0xffffffffffffffff      # FUN_00547d20: one step of the game seed
    rng = e.alloc(8)
    out = e.alloc(4 * 1024)
    e.call(0x5479c0, rng, state & 0xffffffff, difficulty, expansion, ecx=0, edx=out)
    count = e.r32(e.r32(0x744304) + 0xc5c)
    for lid in range(1, count):
        region = e.r32(out + 4 * lid)
        types = []
        for i in range(e.read(region + 0x10, 1)[0]):
            entry = region + 0x14 + 0x34 * i
            sets = [e.read(entry + 4 + 16 * s, 16).hex() for s in range(e.read(entry + 3, 1)[0])]
            types.append(f"{e.r32(entry) & 0xffff}:{'/'.join(sets)}")
        if types: yield lid, types


if __name__ == "__main__":
    e = drlg.boot()
    args = [int(a, 0) for a in sys.argv[1:]]
    for lid, types in regions(e, *args):
        print(lid, " ".join(types))
