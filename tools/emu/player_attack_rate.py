# SPDX-License-Identifier: GPL-3.0-or-later
"""A player's attack animation rate as the oracle: FUN_00623f50 from 0x6241d7
(skill_attack_rate.run_mode) on a player unit.

    uv run python player_attack_rate.py gen 20000 1 > cases.tsv
    uv run python player_attack_rate.py game cases.tsv > g.txt     # `id rate` (unit +0x4c)

A case line is `id mode ias rate68 speed`: the player mode (A1, A2, TH, KK,
SQ; SQ with Jab as the current skill, UseAttackRate set), stat 93
item_fasterattackrate (raw), stat 68 attackrate (raw: 100 for a player with
nothing, minus the weapon's speed, plus a skill's or aura's attackrate) and
the AnimData speed.

As run: rate = FUN_00621740(stat 93: 120 v / (120 + v), v != 0) + stat 68,
-30 in SQ (unit type 0, mode 0x12), clamped 15..175; +0x4c = rate x speed /
100. Stat 68 is not diminished: d2d's rules::attack_speed(speed, ias, wsm,
sias) with 100 - wsm + sias = rate68 (and sias - 30 in SQ).

Hooked as in skill_attack_rate.py; FUN_006235a0 (dual wield) says no, so the
two-weapon average isn't covered.
"""
import random
import sys

import skill_attack_rate

MODES = {"A1": 7, "A2": 8, "TH": 11, "KK": 12, "SQ": 18}
AMAZON = 0


def gen(count, seed):
    rng = random.Random(seed)
    for index in range(count):
        mode = rng.choice(tuple(MODES))
        ias = rng.choice((0, 0, 20, 40, 120, rng.randint(1, 400), -rng.randint(1, 50)))
        rate68 = rng.choice((100, 100, 130, 50, 80, 140, rng.randint(-50, 300)))
        speed = rng.choice((256, 256, 128, 208, rng.randint(1, 512)))
        print(index, mode, ias, rate68, speed)


def game(path):
    import drlg
    from emu import mpq_cache
    e = drlg.boot()
    lines = mpq_cache().read("data\\global\\excel\\skills.txt").decode("latin1").split("\r\n")
    head = lines[0].split("\t")
    skill = next(int(row[head.index("Id")]) for row in (line.split("\t") for line in lines[1:]) if row[0] == "Jab")
    now = {}
    e.hook(0x620250, lambda e: 0x1000, 1)
    e.hook(0x643CE0, lambda e: skill, 3)
    e.hook(0x625500, lambda e: now["ias"] if e.arg(1) == 93 else 0, 3)
    e.hook(0x625480, lambda e: now["rate68"] if e.arg(1) == 68 else 0, 3)
    e.hook(0x6235A0, lambda e: 0, 1)
    e.hook(0x623C80, lambda e: 0, 2)
    e.hook(0x63A7B0, lambda e: 0, 2)
    unit, anim = e.alloc(0x100), e.alloc(0x40)
    for line in open(path, encoding="utf-8"):
        word = line.split()
        now.update(ias=int(word[2]) & 0xFFFFFFFF, rate68=int(word[3]) & 0xFFFFFFFF)
        print(word[0], skill_attack_rate.run_mode(e, unit, anim, 0, AMAZON, MODES[word[1]], int(word[4])))


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]))
    elif len(sys.argv) == 3 and sys.argv[1] == "game":
        game(sys.argv[2])
    else:
        sys.exit(__doc__)
