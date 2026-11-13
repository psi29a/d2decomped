# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's automap cell trees: insert, drop, in-order walk, count, free
(FUN_00457c30, FUN_00457b00, FUN_00458440, FUN_00458470, FUN_00458680,
FUN_00458cf0, and the cel group table FUN_0045a4c0 fills at 0x7a3150).

Usage: uv run python automap_tree.py [--runs N] [--emit ../../tests/automap_tree_cases.inc]
       (export D2_MPQ_DIR=<dir with bin/game.exe> first)

Random runs of (list, cel, x, y) go through the real FUN_00457c30 (a cell
from the chunk pool) and FUN_00457b00 (the AVL insert) into one of a layer
map's four trees (0x7a5164 +8 +0xc +0x10 +0x14), then FUN_00458470 walks each
tree in order and FUN_00458440 counts its unsaved cells. Also checked: the
group table after FUN_0045a4c0 equals the {cel, group} pairs at 0x711258,
the options' defaults it sets, FUN_00458cf0's layer map and FUN_00458680's
free. Only the allocator, the free and the registry read are stubbed. One
line per run: `list:cel,x,y;...` the inserts, TAB, then each list's walk.
"""
import argparse
import random
import sys

import emu

POOL, USED, HEAD, SAVED_OUT, LAYER_MAP, GROUPS = 0x7a5158, 0x7a515c, 0x7a5160, 0x7a5154, 0x7a5164, 0x7a3150


def s16(value):
    """A 16-bit value as signed."""
    return value - 0x10000 if value & 0x8000 else value


class Tree:
    """One game.exe process with the automap's pool and a layer map."""

    def __init__(self):
        """Hook the allocator, free and registry read; run FUN_0045a4c0 (the group table and option defaults)."""
        self.e = emu.Emu()
        self.e.hook(0x40b380, lambda e: e.alloc(0x2804), 2)       # the chunk pool's allocator
        self.freed = 0
        self.e.hook(0x40b3c0, self._free, 2)
        self.e.hook(0x414f10, lambda e: 0, 4)                     # registry read: the default stays
        self.e.hook(0x45a2b0, lambda e: 0, 0)                     # the miniature cel files' loader: not this claim
        self.layer = self.e.alloc(0x1c)
        self.e.call(0x45a4c0)
        self.defaults = [self.e.r32(a) for a in (0x7a51a8, 0x7113e0, 0x7113e4, 0x7113e8, 0x7a51e0, 0x7a51e4, 0x7a51e8)]

    def _free(self, e):
        """FUN_0040b3c0: count the chunks freed."""
        self.freed += 1
        return 0

    def reset(self):
        """A fresh layer map (its trees empty) and an empty pool."""
        self.e.w32(POOL, 0)
        self.e.w32(USED, 0)
        self.e.w32(LAYER_MAP, self.layer)
        for off in range(0, 0x1c, 4):
            self.e.w32(self.layer + off, 0)

    def insert(self, lst, cel, x, y):
        """FUN_00457c30's cell, filled as FUN_00457cf0 does, into its tree by FUN_00457b00."""
        e = self.e
        cell = e.call(0x457c30)
        e.mu.mem_write(cell + 4, (cel & 0xffff).to_bytes(2, "little") + (x & 0xffff).to_bytes(2, "little") + (y & 0xffff).to_bytes(2, "little"))
        return e.call(0x457b00, ecx=cell, edx=self.layer + 8 + 4 * lst)

    def walk(self, lst):
        """The tree in order as (cel, x, y), with FUN_00458440's count of unsaved bytes checked."""
        e = self.e
        root = e.r32(self.layer + 8 + 4 * lst)
        count = e.alloc(4)
        e.call(0x458440, ecx=root, edx=count)
        buf = e.alloc(0x40000)
        e.w32(SAVED_OUT, buf)
        e.call(0x458470, ecx=root)
        length = e.r32(SAVED_OUT) - buf
        assert length == e.r32(count), (length, e.r32(count))
        raw = e.read(buf, length)
        return [tuple(s16(int.from_bytes(raw[i + k:i + k + 2], "little")) for k in (0, 2, 4)) for i in range(0, length, 6)]


def cases(count, seed):
    """Random insert runs: few (x, y) so cells collide, cels from the grouped, ungrouped and past-table ranges."""
    rng = random.Random(seed)
    pool = [0, 1, 2, 3, 6, 7, 8, 11, 12, 13, 14, 20, 38, 21, 39, 46, 47, 48, 49, 51, 52, 53, 54, 60, 70, 61, 71, 120, 169, 171,
            121, 170, 172, 257, 258, 259, 266, 267, 337, 338, 472, 473, 474, 475, 520, 521, 522, 533, 534,   # grouped
            4, 5, 9, 10, 15, 100, 300, 600, 1000, 1258, 1500]                                                  # not
    for _ in range(count):
        size = rng.choice((1, 2, 3, 5, 8, 20, 60, 200))
        spread = rng.choice((1, 2, 3, 6, 40))
        yield [(rng.randrange(4), rng.choice(pool), rng.randrange(spread) - 1, rng.randrange(spread) - 1) for _ in range(size)]


def line(inserts, walks):
    """One case: the inserts, TAB, the four lists' walks."""
    return ";".join(f"{a}:{b},{c},{d}" for a, b, c, d in inserts) + "\t" + "|".join(";".join(f"{a},{b},{c}" for a, b, c in walk) for walk in walks)


def run(runs=600):
    """Drive game.exe over the random runs, checking the table, defaults, layer map and free; returns the case lines."""
    tree = Tree()
    group = {}
    for i in range(0, 0x7113e0 - 0x711258, 8):
        group[tree.e.r32(0x711258 + i)] = tree.e.s32(0x711258 + i + 4)
    table = [tree.e.s32(GROUPS + 4 * c) for c in range(0x800)]
    assert table == [group.get(c, -1) for c in range(0x800)], "group table != the 0x711258 pairs"
    assert tree.defaults == [0, 1, 1, 1, 1, 1, 1], tree.defaults
    out = []
    for inserts in cases(runs, 1):
        tree.reset()
        for item in inserts:
            tree.insert(*item)
        out.append(line(inserts, [tree.walk(lst) for lst in range(4)]))
    # FUN_00458680 (free the pool and empty the map)
    tree.reset()
    for item in next(cases(1, 9)):
        tree.insert(*item)
    chunks, node = 0, tree.e.r32(POOL)
    while node:
        chunks += 1
        node = tree.e.r32(node + 0x2800)
    tree.freed = 0
    tree.e.call(0x458680)
    assert tree.freed == chunks, (tree.freed, chunks)
    assert tree.e.r32(POOL) == 0 and tree.e.r32(USED) == 0 and all(tree.e.r32(tree.layer + o) == 0 for o in (8, 0xc, 0x10, 0x14))
    # FUN_00458cf0 (a layer's map: id, zeros, next = the previous head)
    tree.e.w32(HEAD, 0x1234)
    tree.e.call(0x458cf0, 7)
    made = tree.e.r32(HEAD)
    assert made != 0x1234 and [tree.e.r32(made + o) for o in (0, 4, 8, 0xc, 0x10, 0x14, 0x18)] == [7, 0, 0, 0, 0, 0, 0x1234]
    return out


def main():
    """Run the oracle; --emit freezes the cases for tests/test_automap_tree.cpp."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emit", help="write the frozen cases (tests/automap_tree_cases.inc)")
    parser.add_argument("--runs", type=int, default=600)
    args = parser.parse_args()
    lines = run(args.runs)
    if args.emit:
        with open(args.emit, "w") as out:
            out.write("// from tools/emu/automap_tree.py: game.exe's FUN_00457b00 on random insert runs: { inserts `list:cel,x,y;...`, the four lists' in-order walks `cel,x,y;...|` }\n")
            for text in lines:
                inserts, walks = text.split("\t")
                out.write(f'{{ "{inserts}", "{walks}" }},\n')
    print(f"ok: {len(lines)} automap tree runs, group table, defaults, layer map and free checked")
    return 0


if __name__ == "__main__":
    sys.exit(main())
