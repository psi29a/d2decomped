# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's skill element damage and length as the oracle (FUN_00644d50 /
FUN_00644e40 / FUN_00644f20 with the mastery flag, as FUN_0064b860 calls
them for a missile).

    uv run python skill_damage.py gen 100000 1 > cases.tsv
    uv run python skill_damage.py game cases.tsv > game.tsv
    ../../build/tests/test_game --skill-damage cases.tsv > d2d.tsv
    diff game.tsv d2d.tsv                      # empty
    uv run python skill_damage.py rows         # the element columns of every skill row, as read

A case is `id skill lvl clvl m329 m330 m331 m332 n (skill level)*n`: the
skill and its level (with items), the player's level, the four mastery
stats (fire, lightning, cold, poison) and n other skills' levels (base and
with items alike; a skill not listed is at 0). `game` prints `id min max
len` (256ths, ticks) from the real functions with the data tables loaded
as skill_tips.py loads them.

Hooked (not the claim): FUN_00625480 (the stat getter: stat 0xc gives
clvl, 0x149..0x14c the masteries, else 0), FUN_00643810 (the unit's skill
record by id: a fake record per id) and FUN_006442a0 (its level: the
case's). Everything else runs natively: the level brackets (FUN_00644b70),
the calc machine (FUN_006c0bc0) with its operand table (FUN_00646460),
skill() (FUN_00646c00: FUN_00643810 + FUN_006442a0 flag 1, then the
operand), MulDiv (FUN_00483360) and the mastery (FUN_00644c90).

`gen N SEED` writes every skill with an element at levels 1..40 (alone,
with its neighbours at 20, with masteries 100), then N random cases.
"""
import random
import sys

from unicorn.x86_const import UC_X86_REG_ESI

import skill_tips

RECORD = 0x23c
FAKE = 0x30000000          # fake skill records: FAKE + id * 0x40


class Oracle:
    def __init__(self):
        self.e = e = skill_tips.boot()
        tables = e.r32(0x744304)
        self.skills, self.count = e.r32(tables + 0xb98), e.r32(tables + 0xba0)
        self.unit = e.alloc(0x200)
        self.levels, self.stats = {}, {}
        e.hook(0x625480, lambda e: self.stats.get(e.arg(1), 0), 3)
        e.hook(0x643810, lambda e: FAKE + 0x40 * (e.mu.reg_read(UC_X86_REG_ESI) & 0xffff), 0)
        e.hook(0x6442a0, lambda e: self.levels.get((e.arg(1) - FAKE) // 0x40, 0), 3)

    def row(self, skill):
        e, r = self.e, self.skills + skill * RECORD
        return {"etype": e.read(r + 0x1dc, 1)[0], "emin": e.s32(r + 0x1e0), "emax": e.s32(r + 0x1e4),
                "hitshift": e.read(r + 0x1a4, 1)[0], "edmgsym": e.s32(r + 0x210), "elen": e.s32(r + 0x214),
                "eminlev1": e.s32(r + 0x1e8), "emaxlev1": e.s32(r + 0x1fc)}

    def run(self, skill, lvl, clvl, masteries, levels):
        self.levels = dict(levels)
        self.levels[skill] = lvl
        self.stats = {0xc: clvl, 0x149: masteries[0], 0x14a: masteries[1], 0x14b: masteries[2], 0x14c: masteries[3]}
        lo = self.e.call(0x644d50, self.unit, skill, lvl, 1)
        hi = self.e.call(0x644e40, self.unit, skill, lvl, 1)
        ln = self.e.call(0x644f20, self.unit, skill, lvl, 1)
        return tuple(v - (1 << 32 if v >> 31 else 0) for v in (lo, hi, ln))


def parse(line):
    f = [int(x) for x in line.split()]
    n = f[8]
    return f[0], f[1], f[2], f[3], f[4:8], [(f[9 + 2 * k], f[10 + 2 * k]) for k in range(n)]


def gen(n, seed, o):
    rng = random.Random(seed)
    elemental = [s for s in range(o.count) if o.row(s)["etype"] != 0]
    out = []
    for s in elemental:
        near = [(t, 20) for t in range(max(s - 15, 0), min(s + 16, o.count)) if t != s]
        for lvl in range(1, 41):
            out.append((s, lvl, 1, [0, 0, 0, 0], []))
            out.append((s, lvl, 50, [0, 0, 0, 0], near))
            out.append((s, lvl, 50, [100, 100, 100, 100], near))
            out.append((s, lvl, 99, [37, 37, 37, 37], near))
    for _ in range(n):
        s = rng.choice(elemental)
        lvl = rng.choice([rng.randint(1, 20), rng.randint(1, 60), rng.choice([1, 2, 8, 9, 16, 17, 22, 23, 28, 29, 30])])
        clvl = rng.randint(1, 99)
        m = [rng.choice([0, 0, rng.randint(0, 600)]) for _ in range(4)]
        k = rng.randint(0, 6)
        pairs = [(rng.randint(max(s - 15, 0), min(s + 15, o.count - 1)), rng.randint(0, 40)) for _ in range(k)]
        out.append((s, lvl, clvl, m, pairs))
    for i, (s, lvl, clvl, m, pairs) in enumerate(out):
        print(i, s, lvl, clvl, *m, len(pairs), *(v for p in pairs for v in p))


def level_bonus(lev, lvl):
    if lvl < 2: return 0
    if lvl > 28: return (lvl - 28) * lev[4] + (lev[3] + lev[2]) * 6 + lev[1] * 8 + lev[0] * 7
    if lvl > 22: return (lvl - 22) * lev[3] + lev[2] * 6 + lev[1] * 8 + lev[0] * 7
    if lvl > 16: return (lvl - 16) * lev[2] + lev[1] * 8 + lev[0] * 7
    if lvl > 8: return (lvl - 8) * lev[1] + lev[0] * 7
    return (lvl - 1) * lev[0]


def main():
    args = sys.argv[1:]
    if not args: raise SystemExit(__doc__)
    o = Oracle()
    if args[0] == "gen":
        gen(int(args[1]), int(args[2]), o)
    elif args[0] == "game":
        with open(args[1]) as f:
            for line in f:
                cid, s, lvl, clvl, m, pairs = parse(line)
                print(cid, *o.run(s, lvl, clvl, m, pairs))
    elif args[0] == "rows":
        for s in range(o.count):
            r = o.row(s)
            if r["etype"]: print(s, r)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
