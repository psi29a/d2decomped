# SPDX-License-Identifier: GPL-3.0-or-later
"""A Blessed Hammer hit on a monster, end to end, by game.exe's own code.

    uv run python hammer_hit.py gen N SEED [plain] > cases.tsv      # plain: only monsters that are neither undead nor demon
    uv run python hammer_hit.py game cases.tsv > game.tsv
    ../../build/tests/test_hammer_hit --hammer-hit cases.tsv | grep -E '^[0-9]+ ' > d2d.tsv
    diff game.tsv d2d.tsv                       # empty with the fix

A case is `id lvl vigor aim conc monster res life lo32 hi32`: the hammer's
level, the levels of its two synergy skills (Vigor, Blessed Aim), the
damagepercent of the caster's Concentration state (-1000: not on the caster),
the monster's MonStats row (its undead / demon flags are game.exe's own), its
magic resistance, its life in whole points and the missile's seed.
`game` prints `id min max life`: the missile's magic min / max after the
caster's Concentration influence (256ths) and the monster's life after the
roll, the undead / demon bonus and FUN_0057c6c0 (256ths).

What runs for real: FUN_00644d50 / FUN_00644e40 (the skill's min / max with
synergies, skill 112), FUN_006461d0 (the influence of the caster's Concentration
state, the do FUN_005d0040 scales the missile's stats 0x34 / 0x35 by it with
FUN_00483360 at 0x5d011a), FUN_005ad2f0 (Blessed Hammer's damage function 5:
FUN_005a8c70 / FUN_005a8910 roll the record on the missile's seed, FUN_0063e990 /
FUN_0063e940 test the monster, FUN_005a9270 adds the bonus), FUN_0057c6c0
(resistance and life). Hooked (not the claim): the stat getter / setter
(FUN_00625480 / FUN_00627260, a dict), the caster's state test FUN_00639df0 and
its stat list (FUN_006256b0 / FUN_00625d00), max life, the difficulty row and
the side effects of a hit (events, stun, freeze, poison, logging).

Domain: a plain monster (no hireling), magic damage reduction 0, absorb 0, no
pierce (magic has none), Concentration alone (the only state FUN_006461d0 reads).
"""
import random
import sys

import skill_damage
import skill_tips

MASK = 0xFFFFFFFF
BH, VIGOR, AIM, CONC = 112, 115, 108, 113
MISSILE = 92                                    # blessedhammer, Missiles.txt row (D)
RES_MAGIC = 0x25                                # stat 37: magicresist


def s32(v):
    return v - (1 << 32) if v >> 31 else v


class Hit:
    def __init__(self):
        e = self.e = skill_tips.boot()
        self.stats = {}
        self.pct = 0
        self.max_life = 0
        self.game = e.alloc(0x100)
        game = e.view("game", self.game)
        game.difficulty, game.expansion = 1, 1
        self.caster = e.alloc(0x200)
        e.w32(self.caster, 0)                                              # a player
        self.target = e.alloc(0x100)
        e.w32(self.target, 1)                                              # a monster
        self.missile = e.alloc(0x100)
        e.w32(self.missile, 8)                                             # a missile
        e.w32(self.missile + 4, MISSILE)
        self.rec = e.alloc(0x80)
        self.diffrow = e.alloc(0x40)
        e.hook(0x625480, lambda e: self.stats.get((e.arg(0), e.arg(1)), 0), 3)
        e.hook(0x627260, lambda e: self.stats.__setitem__((e.arg(0), e.arg(1)), s32(e.arg(2))) or 0, 4)
        e.hook(0x625d10, lambda e: self.max_life, 1)
        e.hook(0x611d30, lambda e: self.diffrow, 1)
        e.hook(0x639df0, lambda e: 1, 2)                                   # the caster has state 0x2a
        e.hook(0x6256b0, lambda e: 0x31000000, 2)                          # its stat list: a fake
        e.hook(0x625d00, lambda e: self.pct & MASK, 3)                     # damagepercent of that list
        e.stub(0, {0x63ee90: 1, 0x63edc0: 1, 0x63e9f0: 2, 0x5c0c30: 3, 0x5347d0: 7,
                   0x5347e0: 3, 0x61ab00: 1, 0x5541b0: 0, 0x621d50: 2, 0x57aa60: 0, 0x57aaa0: 0,
                   0x57aae0: 2, 0x57af80: 2, 0x57b230: 2, 0x57ac50: 3, 0x57add0: 3, 0x5d6410: 0})
        e.stub(1, {0x554200: 1, 0x620bb0: 1, 0x457490: 0})

    def flags(self, monster):
        self.e.w32(self.target + 4, monster)
        return self.e.call(0x63e990, self.target) & 1 or self.e.call(0x63e940, self.target) & 1

    def influence(self, pct):
        self.pct = pct
        return s32(self.e.call(0x6461d0, self.caster, BH))

    def run(self, lo, hi, infl, monster, res, life, seed):
        e = self.e
        self.stats.clear()
        self.max_life = life << 8
        self.stats[(self.target, 6)] = life << 8
        self.stats[(self.target, 7)] = life << 8
        self.stats[(self.target, RES_MAGIC)] = res & MASK
        e.w32(self.target + 4, monster)
        if infl:
            lo = s32(e.call(0x483360, 100, ecx=lo, edx=100 + infl))
            hi = s32(e.call(0x483360, 100, ecx=hi, edx=100 + infl))
        self.stats[(self.missile, 0x34)] = lo & MASK
        self.stats[(self.missile, 0x35)] = hi & MASK
        e.w32(self.missile + 0x20, seed[0] & MASK)
        e.w32(self.missile + 0x24, seed[1] & MASK)
        e.mu.mem_write(self.rec, b"\0" * 0x80)
        e.call(0x5ad2f0, self.target, self.rec, ecx=self.game, edx=self.missile)
        fields = [(o, e.s32(self.rec + o)) for o in range(8, 0x48, 4) if e.s32(self.rec + o)]
        e.w32(self.rec, 1)
        e.w32(self.rec + 4, 1)                                             # flags 1 (FUN_005ad730), result word: hit
        e.call(0x57c6c0, self.target, 1, self.rec, ecx=self.game, edx=self.caster)
        return lo, hi, fields, self.stats[(self.target, 6)]


def parse(line):
    f = [int(x) for x in line.split()]
    return f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], (f[8] & MASK, f[9] & MASK)


def gen(n, seed, pool):
    rng = random.Random(seed & MASK)
    out = []

    def case(lvl, vigor, aim, conc, monster, res, life):
        out.append((lvl, vigor, aim, conc, monster, res, life, (rng.getrandbits(32), rng.getrandbits(32))))

    # Structured: every monster id (734) at a few Concentration levels (its damagepercent
    # 60 + 15 (L-1); -1000 off) and hammer levels, resistance 0 and -50 / 50.
    for monster in pool:
        for conc in (-1000, 60, 345):
            case(rng.choice((1, 10, 20)), rng.choice((0, 20)), rng.choice((0, 20)), conc, monster, rng.choice((0, 0, 50, -50)), rng.choice((100, 5000)))
    # Concentration levels 1..60 and hammer levels 1..40 on one undead, one demon, one neither.
    for conc_level in range(0, 61):
        for lvl in range(1, 41):
            for monster in rng.sample(pool, 3):
                case(lvl, rng.choice((0, 5, 20)), rng.choice((0, 5, 20)), -1000 if conc_level == 0 else 60 + 15 * (conc_level - 1), monster, rng.choice((0, 25, -100)), rng.choice((50, 20000)))
    for _ in range(n):
        conc = rng.choice((-1000, rng.randint(-20, 1000)))
        case(rng.randint(1, 40), rng.randint(0, 30), rng.randint(0, 30), conc, rng.choice(pool), rng.choice((rng.randint(-100, 99), 0, 100, 120)), rng.randint(1, 30000))
    for i, (lvl, vigor, aim, conc, monster, res, life, sd) in enumerate(out):
        print(i, lvl, vigor, aim, conc, monster, res, life, *sd)


def main():
    args = sys.argv[1:]
    if not args:
        raise SystemExit(__doc__)
    if args[0] == "gen":
        hit = Hit()
        count = hit.e.r32(hit.e.r32(0x744304) + 0xa80)
        pool = [m for m in range(count) if "plain" not in args[3:] or not hit.flags(m)]
        gen(int(args[1]), int(args[2]), pool)
        return
    if args[0] != "game":
        raise SystemExit(__doc__)
    damage, hit = skill_damage.Oracle(), Hit()
    with open(args[1]) as f:
        for line in f:
            cid, lvl, vigor, aim, conc, monster, res, life, seed = parse(line)
            lo, hi, _ = damage.run(BH, lvl, 30, [0, 0, 0, 0], [(VIGOR, vigor), (AIM, aim)])
            infl = hit.influence(conc) if conc != -1000 else 0
            lo, hi, fields, after = hit.run(lo, hi, infl, monster, res, life, seed)
            assert all(o == 0x20 for o, _ in fields), fields                 # the magic field only
            print(cid, lo, hi, after)


if __name__ == "__main__":
    main()
