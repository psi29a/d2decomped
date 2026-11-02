# SPDX-License-Identifier: GPL-3.0-or-later
"""A Sorceress missile's hit on a monster, end to end, by game.exe's own code.

    uv run python missile_hit.py gen N SEED > cases.tsv
    uv run python missile_hit.py game cases.tsv > game.tsv
    ../../build/tests/test_missile_hit --missile-hit cases.tsv | grep -E '^[0-9]+ ' > d2d.tsv
    diff game.tsv d2d.tsv                      # empty with the fix

A case is `id skill lvl clvl m329 m330 m331 m332 res pierce life lo32 hi32 n
(skill level)*n`: the skill and its level, the player's level, the four mastery
stats, the monster's resistance to the skill's element, the caster's pierce of
that element, the monster's life in whole points, the missile's seed (low and
high word) and n other skills' levels (the synergies). `game` prints `id min
max life`: the element's min and max (256ths, FUN_00644d50 / FUN_00644e40 with
the mastery flag, as FUN_0064b860 builds the missile) and the monster's life
after the hit (256ths): FUN_005a89a0 rolls the record from the missile's stats
on the missile's seed (FUN_005a8910: both stats over 0, else 0; min + rand(max
- min)), then FUN_0057c6c0 applies that record (resistance, pierce, MulDiv,
life). To stderr: how many cases a copy of d2d's roll (min + rand(max - min +
1), rolled whenever max > 0) and of game.exe's reading get wrong.

Hooked (not the claim): skill_damage.py's (stat getter, the unit's skill record
and level) for the min and max; life_apply.py's (stat getter / setter, max life,
unit kinds, difficulty row, events, cold / poison / burn side effects) for the
roll and the hit. The roll and the hit share one stat dict: the missile carries
only its own element pair and no mastery (FUN_0064ac60 sets none, so the roll's
mastery read is 0).

Domain: a plain monster (no hireling), magic damage reduction and absorb 0,
pierce only against a non-immune, one element per skill (the Sorceress's).
"""
import random
import sys

import life_apply
import skill_damage

MASK = 0xFFFFFFFF
# skill -> (element stats min / max, resistance stat, pierce stat, record field)
ELEMENT = {"fire": (0x30, 0x31, 0x27, 0x14d, 0x10), "ltng": (0x32, 0x33, 0x29, 0x14e, 0x1c), "cold": (0x36, 0x37, 0x2b, 0x14f, 0x24)}
SKILLS = {36: "fire", 38: "ltng", 39: "cold", 44: "cold", 45: "cold", 48: "ltng", 49: "ltng", 53: "ltng"}
SYNERGIES = {36: (47, 56), 38: (49,), 39: (44, 45, 55, 59, 64), 44: (59, 64), 45: (39, 59, 64), 48: (), 49: (38, 53, 48), 53: (38, 49, 48)}
MASTERY = {"fire": 0, "ltng": 1, "cold": 2}


def rand(seed, bound):
    """FUN_0045c3e0 on a {low, high} seed (d2d's Rng)."""
    if bound < 1:
        return 0
    product = seed[0] * 0x6AC690C5 + seed[1]
    seed[0], seed[1] = product & MASK, (product >> 32) & MASK
    return seed[0] & (bound - 1) if bound & (bound - 1) == 0 else seed[0] % bound


def roll_game(lo, hi, seed):
    if lo <= 0 or hi <= 0:
        return 0
    lo, hi = min(lo, hi), max(lo, hi)
    return lo + rand(list(seed), hi - lo)


def roll_d2d(lo, hi, seed):
    """components/rules/combat.hpp missile_blow before the fix: Rng::range, inclusive."""
    hi = max(hi, lo)
    if hi <= 0:
        return 0
    return lo + rand(list(seed), hi - lo + 1) if hi > lo else lo


class Hit(life_apply.Game):
    def __init__(self):
        super().__init__()
        self.missile = self.e.alloc(0x100)

    def run(self, element, lo, hi, res, pierce, life, seed):
        smin, smax, sres, spierce, field = ELEMENT[element]
        e = self.e
        self.reset(life, 0, 0)
        self.stats[(self.target, 0x29)] = 0
        self.stats[(self.target, sres)] = res & MASK
        self.stats[(self.attacker, spierce)] = pierce
        self.stats[(self.missile, smin)] = lo & MASK
        self.stats[(self.missile, smax)] = hi & MASK
        e.w32(self.missile + 0x20, seed[0] & MASK)
        e.w32(self.missile + 0x24, seed[1] & MASK)
        e.mu.mem_write(self.rec, b"\0" * 0x80)
        e.call(0x5a89a0, self.missile, 0, self.rec)
        rolled = e.s32(self.rec + field)
        e.w32(self.rec, 1)
        e.w32(self.rec + 4, 1)                                  # flags 1 (FUN_005ad730), result word: hit
        e.call(0x57c6c0, self.target, 1, self.rec, ecx=self.game, edx=self.attacker)
        return rolled, self.life()


def parse(line):
    f = [int(x) for x in line.split()]
    n = f[13]
    return f[0], f[1], f[2], f[3], f[4:8], f[8], f[9], f[10], (f[11] & MASK, f[12] & MASK), [(f[14 + 2 * k], f[15 + 2 * k]) for k in range(n)]


def gen(n, seed):
    rng = random.Random(seed & MASK)
    out = []

    def case(skill, lvl, clvl, mastery, syn, res, pierce, life):
        m = [0, 0, 0, 0]
        m[MASTERY[SKILLS[skill]]] = mastery
        out.append((skill, lvl, clvl, m, res, pierce, life, (rng.getrandbits(32) & MASK, rng.getrandbits(32) & MASK), [(s, syn) for s in SYNERGIES[skill]]))

    # Structured: every plan skill at levels 1..20, synergies 0 / 1 / 5 / 10 / 20,
    # Lightning Mastery's stat at 0 and its levels 1, 5, 10, 20 (50 + 12 (lvl - 1),
    # Skills.txt Param1 / Param2), against the resistances that matter.
    for skill in SKILLS:
        for lvl in range(1, 21):
            for syn in (0, 1, 5, 10, 20):
                for mastery in (0, 50, 98, 158, 278):
                    for res in (-100, -50, 0, 20, 25, 33, 50, 75, 99, 100, 150):
                        pierce = rng.choice((0, 0, 15, 60)) if res < 100 else 0
                        case(skill, lvl, rng.randint(1, 99), mastery, syn, res, pierce, rng.choice((1, 2, 5, 20, 60, 300)))
    for _ in range(n):
        skill = rng.choice(list(SKILLS))
        res = rng.choice((rng.randint(-100, 99), rng.randint(100, 150), 0))
        pierce = 0 if res >= 100 or rng.random() < 0.6 else rng.randint(0, 100)
        case(skill, rng.randint(1, 20) if rng.random() < 0.8 else rng.randint(21, 40), rng.randint(1, 99),
             rng.choice((0, 0, rng.randint(1, 400))), rng.choice((0, rng.randint(0, 20), rng.randint(0, 40))), res, pierce, rng.randint(1, 400))
    for i, (s, lvl, clvl, m, res, pierce, life, sd, pairs) in enumerate(out):
        print(i, s, lvl, clvl, *m, res, pierce, life, *sd, len(pairs), *(v for p in pairs for v in p))


def main():
    args = sys.argv[1:]
    if not args:
        raise SystemExit(__doc__)
    if args[0] == "gen":
        gen(int(args[1]), int(args[2]))
        return
    if args[0] != "game":
        raise SystemExit(__doc__)
    damage, hit = skill_damage.Oracle(), Hit()
    bad = {"d2d": 0, "reading": 0}
    cases = 0
    with open(args[1]) as f:
        for line in f:
            cid, skill, lvl, clvl, m, res, pierce, life, seed, pairs = parse(line)
            lo, hi, _ = damage.run(skill, lvl, clvl, m, pairs)
            rolled, after = hit.run(SKILLS[skill], lo, hi, res, pierce, life, seed)
            bad["d2d"] += roll_d2d(lo, hi, seed) != rolled
            bad["reading"] += roll_game(lo, hi, seed) != rolled
            cases += 1
            print(cid, lo, hi, after)
    print(f"{cases} cases: d2d's roll wrong on {bad['d2d']}, the reading wrong on {bad['reading']}", file=sys.stderr)


if __name__ == "__main__":
    main()
