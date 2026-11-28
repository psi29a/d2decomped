# SPDX-License-Identifier: GPL-3.0-or-later
"""Hydra's cast (srvdofunc 144, FUN_005ca910) as the oracle: which of its three heads are made, of which row, where.

    uv run python hydra.py gen 20000 1 > cases.txt     # `px py town spot near (dx dy) x 3`
    uv run python hydra.py game < cases.txt > game.txt   # each case and `| row,x,y ...` (`-` for a head not made)
    ../../build/tests/test_hydra --hydra < cases.txt | grep ' | ' > d2d.txt
    uv run python hydra.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: the skill's point (FUN_0056d2c0, subtiles; at least 8 on each axis), `town` (the point's room is a town's,
FUN_0061ab00), then two 3-bit masks in the heads' order: `spot`, the heads whose own subtile the spawn finds free
(FUN_005b2f20 at radius -1), and `near`, those it finds room for within 4 when it isn't (radius 4), each then at its
own (dx, dy) off its spot (the case's three pairs, -4..4). FUN_005ca910 and the spawn FUN_0056d940 run for real: none
in a town; else three heads, head k of the summon row plus k (hydra1..3) at the point plus 0x6e312c[k] (x) /
0x6e3120[k] (y), each through FUN_0056d940 with request kind 1 (no flag 4: radius -1, then 4). Each line: the case,
then each head's `row,x,y` (its row less the summon row, the subtile it got) or `-`.
Hooked (inputs and side effects, not the claim): the summon row FUN_0056e620 (100), the calcs FUN_00646ca0, the
caster's room FUN_00620bb0, the point FUN_0056d2c0, the point's room FUN_00463740, the town test FUN_0061ab00
(`town`), the caster's kind FUN_0063ee90 / FUN_0058f0d0, FUN_004e6ca0, the spawn's search FUN_005b2f20 (`spot` /
`near`), FUN_0056d940's set-up after it (FUN_0056d8d0, FUN_00639df0, FUN_005b0e00, FUN_00573780, FUN_00540e60,
FUN_005417d0, FUN_0058f030), and the head's FUN_0056deb0 / FUN_005c4470 / FUN_00541860 / FUN_005b0d70 /
FUN_0044be50 / FUN_006259b0 / FUN_005543b0. A fake data block holds one Skills row.
"""
import random
import sys

import emu

USAGE = "usage: hydra.py gen N SEED | game < CASES | compare GAME D2D"
SKILL, ROW = 1, 100


def gen(count, seed):
    rng = random.Random(seed)
    offsets = lambda: [rng.randint(-4, 4) for _ in range(6)]
    for px, py in ((1000, 1000), (8, 8), (8, 30000), (30000, 8)):
        for town, spot, near in ((0, 7, 0), (1, 7, 7), (0, 0, 0), (0, 0, 7), (0, 5, 2), (0, 2, 0)):
            print(px, py, town, spot, near, *offsets())
    for _ in range(count):
        px, py = rng.choice((rng.randint(8, 60), rng.randint(8, 30000))), rng.choice((rng.randint(8, 60), rng.randint(8, 30000)))
        print(px, py, int(rng.random() < 0.1), rng.choice((7, 7, rng.randint(0, 7))), rng.choice((7, 0, rng.randint(0, 7))), *offsets())


class Rig:
    def __init__(self):
        e = self.e = emu.Emu()
        data, skills = e.alloc(0x1000), e.alloc(0x23c * 4)
        e.w32(0x744304, data)
        e.w32(data + 0xb98, skills)
        e.w32(data + 0xba0, 4)
        e.w32(data + 0xbf0, 0x100)
        e.mu.mem_write(skills + SKILL * 0x23c + 0xbe, b"\x01")      # its summon mode, a valid one
        self.game, self.caster = e.alloc(0x1000), e.alloc(0x200)
        self.case, self.heads, self.head = {}, [], -1
        hooks = ((0x56e620, lambda e: ROW, 5), (0x646ca0, lambda e: 1, 4), (0x620bb0, lambda e: 0x1000, 1), (0x56d2c0, self.point, 2),
                 (0x463740, lambda e: 0x2000, 1), (0x61ab00, lambda e: self.case["town"], 1), (0x5b2f20, self.spawn, 6))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)
        e.stub(0, {0x63ee90: 1, 0x58f0d0: 0, 0x4e6ca0: 0, 0x56d8d0: 1, 0x639df0: 2, 0x5b0e00: 2, 0x573780: 0, 0x540e60: 2, 0x5417d0: 4,
                   0x58f030: 4, 0x56deb0: 2, 0x5c4470: 4, 0x541860: 0, 0x5b0d70: 2, 0x44be50: 0, 0x6259b0: 1, 0x5543b0: 1})

    def point(self, e):
        e.w32(e.arg(0), self.case["px"])
        e.w32(e.arg(1), self.case["py"])
        return 1

    def spawn(self, e):
        signed = lambda value: value - (1 << 32) if value & 0x80000000 else value
        x, y, row, radius = signed(e.arg(0)), signed(e.arg(1)), signed(e.arg(2)), signed(e.arg(4))
        if radius == -1:
            self.head += 1                   # one radius -1 search a head, first
        k = self.head
        if radius == -1 and (self.case["spot"] >> k) & 1:
            at = (x, y)
        elif radius == 4 and (self.case["near"] >> k) & 1:
            at = (x + self.case["offsets"][2 * k], y + self.case["offsets"][2 * k + 1])
        else:
            return 0
        self.heads[k] = (row - ROW, at[0], at[1])
        unit = e.alloc(0x200)
        e.w32(unit, 1)                       # a monster
        return unit

    def run(self, px, py, town, spot, near, offsets):
        self.case = {"px": px, "py": py, "town": town, "spot": spot, "near": near, "offsets": offsets}
        self.heads, self.head = [None] * 3, -1
        self.e.mu.mem_write(self.caster, bytes(0x200))
        self.e.call(0x5ca910, SKILL, 1, ecx=self.game, edx=self.caster)
        if not town and self.head != 2:
            sys.exit(f"{self.head + 1} heads tried, not 3: {px} {py}")
        return " ".join("-" if head is None else f"{head[0]},{head[1]},{head[2]}" for head in self.heads)


def game():
    rig = Rig()
    for line in sys.stdin:
        field = line.split()
        if field:
            values = [int(f) for f in field[:11]]
            print(line.strip(), "|", rig.run(*values[:5], values[5:]))


def rows(path):
    out = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            case, bar, result = line.partition("|")
            case, result = case.split(), result.split()
            heads_ok = len(result) == 3 and all(head == "-" or (len(head.split(",")) == 3 and all(part.lstrip("-").isdigit() for part in head.split(",")))
                                                for head in result)
            if not bar or len(case) != 11 or not all(f.lstrip("-").isdigit() for f in case) or not heads_ok:
                sys.exit(f"malformed line in {path}: {line.strip()!r} (11 case fields, then 3 heads)")
            out.append((case, result))
    return out


def compare(game_path, d2d_path):
    game_rows, d2d_rows = rows(game_path), rows(d2d_path)
    if not game_rows or len(game_rows) != len(d2d_rows):
        sys.exit(f"empty or unequal: {len(game_rows)} game, {len(d2d_rows)} d2d (an empty file is not a pass)")
    differ = 0
    for (g_case, g), (d_case, d) in zip(game_rows, d2d_rows, strict=True):
        if g_case != d_case:
            sys.exit(f"cases out of step: game {' '.join(g_case)}, d2d {' '.join(d_case)}")
        if g != d:
            differ += 1
            if differ <= 20:
                print(f"DIFF {' '.join(g_case)}: game {' '.join(g)}, d2d {' '.join(d)}")
    print(f"{len(game_rows)} cases, {differ} differ")
    sys.exit(1 if differ else 0)


def main():
    a = sys.argv[1:]
    if not a or (a[0], len(a)) not in {("gen", 3), ("game", 1), ("compare", 3)}:
        sys.exit(USAGE)
    if a[0] == "gen":
        gen(int(a[1]), int(a[2]))
    elif a[0] == "game":
        game()
    else:
        compare(a[1], a[2])


if __name__ == "__main__":
    main()
