# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's shift-click to the belt (C->S 0x63) as the oracle.

A shift + left click on an inventory item (the client's FUN_0048ffe0 at
0x49029d: MK_SHIFT, FUN_0062bad0, FUN_00628250 == 0) sends 0x63; the
server's FUN_0054d520 then, the hand empty, refuses an item whose own type
isn't Beltable (FUN_0062bad0) or that isn't on inventory page 0
(FUN_00628250), asks FUN_0063c600 for the box (no box: nothing moves, the
item stays in the pack) and moves it there (FUN_0063c4f0). This script runs
those three functions natively on the loaded 1.14d tables, in
FUN_0054d520's order; NOTHING is hooked. Not run: FUN_0054d520's unit
lookups, its owner / mode check (FUN_00549150), the cursor test and the
trade check (FUN_00535610): d2d's test feeds an item it owns, stored, the
hand empty. The units are fakes: an item is {type 4, class, +0x14 item
data with the page at +0x45}; the inventory is {0x1020304, grids at +0x14,
count +0x18} with grid 0 (body, 13x1, belt at slot 8) and grid 1 (the
belt, 16x1), as FUN_0063add0 finds them.

    uv run python auto_belt.py gen 100000 1 > cases.tsv
    uv run python auto_belt.py game cases.tsv > game.tsv
    ../../build/tests/test_game --auto-belt cases.tsv | grep -E '^[0-9]+ ' > d2d.tsv
    diff game.tsv d2d.tsv

A case is `id page belt b0 .. b15 item`: the clicked item's page (0
inventory, 3 cube, 4 stash), the equipped belt's code (`-`: none), the 16
belt boxes (`-`: empty; box = row * 4 + column, row 0 the bottom) and the
clicked item's code. `game` prints `id box`: the box the item moves to, or
-1 when it stays.
"""
import random
import sys

import skill_tips

KINDS = ["hp1", "hp2", "hp3", "hp4", "hp5", "mp1", "mp2", "mp3", "mp4", "mp5", "rvs", "rvl", "vps", "yps", "wms",
         "isc", "tsc", "elx", "hpo", "mpo", "rps", "bpl", "0sc"]
PICKS = KINDS + ["key", "rin", "jew", "amu", "aqv", "gcv"]   # not Beltable or not 1x1
BELTS = ["-", "lbl", "vbl", "mbl", "tbl", "hbl", "zlb", "uhc"]   # belts.txt 2, 1, 4, 0, 5, 3, 6, 6


class Oracle:
    def __init__(self):
        self.e = e = skill_tips.boot()
        self.cls = {}
        for c in range(e.r32(0x96ca58)):
            rec = e.call(0x6335f0, c)
            self.cls.setdefault(e.read(rec + 0x80, 4).decode("latin1").strip(), c)
        self.units = {}
        self.boxes = e.alloc(16 * 4)
        self.body = e.alloc(13 * 4)
        grids = e.alloc(2 * 0x10)
        for g, (w, h, items) in enumerate(((13, 1, self.body), (16, 1, self.boxes))):
            e.mu.mem_write(grids + g * 0x10 + 8, bytes([w, h]))
            e.w32(grids + g * 0x10 + 0xc, items)
        self.inv = e.alloc(0x40)
        e.w32(self.inv, 0x1020304)
        e.w32(self.inv + 0x14, grids)
        e.w32(self.inv + 0x18, 2)
        self.out = e.alloc(4)

    def unit(self, code, slot, page=0):
        key = (code, slot, page)
        if key not in self.units:
            u, data = self.e.alloc(0x18), self.e.alloc(0x50)
            self.e.w32(u, 4)
            self.e.w32(u + 4, self.cls[code])
            self.e.w32(u + 0x14, data)
            self.e.mu.mem_write(data + 0x45, bytes([page]))
            self.units[key] = u
        return self.units[key]

    def run(self, page, belt, boxes, item):
        e = self.e
        e.w32(self.body + 8 * 4, 0 if belt == "-" else self.unit(belt, "belt"))
        for b, code in enumerate(boxes):
            e.w32(self.boxes + 4 * b, 0 if code == "-" else self.unit(code, b))
        it = self.unit(item, "clicked", page)
        if not e.call(0x62bad0, it) or e.call(0x628250, it) & 0xff:   # 0x54d58e, 0x54d598
            return -1
        e.w32(self.out, 0)                                           # 0x54d5b8
        if not e.call(0x63c600, self.inv, it, self.out) or e.s32(self.out) < 0:   # 0x54d5bf..0x54d5d0
            return -1
        return e.s32(self.out)


def rows(belt):
    return {"-": 1, "lbl": 2, "vbl": 2, "mbl": 3, "tbl": 3}.get(belt, 4)


def gen(n, seed):
    """Page 0: every belt x bottom row over {-, hp1, mp1, rvs, isc} x {bottom only, columns full} x every item; then
    n random belts whose columns are stacks (no gaps, any kinds, up to the belt's rows), a tenth of them on page 3
    or 4."""
    rng = random.Random(seed)
    out, few = [], ["-", "hp1", "mp1", "rvs", "isc"]
    for belt in BELTS:
        for a in few:
            for b in few:
                for c in few:
                    for d in few:
                        for full in (0, 1):
                            boxes = ["-"] * 16
                            for col, k in enumerate((a, b, c, d)):
                                for r in range(rows(belt) if full and k != "-" else (1 if k != "-" else 0)):
                                    boxes[r * 4 + col] = k
                            for pick in PICKS:
                                out.append((0, belt, boxes, pick))
    for _ in range(n):
        belt = rng.choice(BELTS)
        boxes = ["-"] * 16
        for col in range(4):
            h = rng.randint(0, 4 if rng.random() < 0.1 else rows(belt))   # 10 %: rows above the belt (a smaller belt swapped in)
            same = rng.random() < 0.6
            k0 = rng.choice(KINDS)
            for r in range(h):
                boxes[r * 4 + col] = k0 if same else rng.choice(KINDS)
        page = rng.choice((3, 4)) if rng.random() < 0.1 else 0
        out.append((page, belt, boxes, rng.choice(PICKS)))
    for i, (page, belt, boxes, pick) in enumerate(out):
        print(i, page, belt, " ".join(boxes), pick)


def main():
    if len(sys.argv) > 3 and sys.argv[1] == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]))
    elif len(sys.argv) > 2 and sys.argv[1] == "game":
        o = Oracle()
        with open(sys.argv[2]) as f:
            for line in f:
                p = line.split()
                if len(p) != 20: continue
                print(p[0], o.run(int(p[1]), p[2], p[3:19], p[19]))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
