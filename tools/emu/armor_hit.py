# SPDX-License-Identifier: GPL-3.0-or-later
"""Frozen Armor's answer to a melee striker (aura event 2, damagedinmelee) by game.exe's own code.

    python armor_hit.py gen [N] [SEED] > cases.tsv
    python armor_hit.py game cases.tsv > game.tsv          # FUN_005cac50 natively
    <build>/tests/test_armor_hit --armor-hit cases.tsv > d2d.tsv
    python armor_hit.py compare cases.tsv game.tsv d2d.tsv

Runs the real FUN_005cac50 (ECX game; wearer, striker, the striker's hit
record, skill, level): it returns at once unless the striker is a player or
a monster and the striker's record +8 (physical damage) is over 0, then
zeroes a 0x70 record, sets +4 |= 0x20 (no on-hit events), puts calc1
(Skills +0x138, on the wearer) in +0x34 (the freeze length) and calls
FUN_0057c6c0(game, wearer -> striker, flag 1, record): FUN_0057c1e0 cuts
+0x34 by the striker's cold resistance less the wearer's cold pierce (row 6
of the table at 0x732980), FUN_0057af80 gets +0x30 (0: no chill) and
FUN_0057b230 +0x34: a MonStats boss, monster-data flag 8 or a hireling is
chilled for it (FUN_0057af80), anything else is frozen only at a negative
coldeffect, for the length / MonsterFreezeDivisor, and not chilled.

Hooked (not the claim): everything freeze_rules.Game hooks, FUN_00646ca0
(the calc: returns `frames(level, synergy)`, calc1 of Frozen Armor from
Skills.txt; the d2d side prints its own calc1, compared as a column of its
own) and FUN_00621e40 (the overlay). The Skills row is a fake one: only
+0x138 (the calc index) and +0x110 = -1 (no overlay) matter to FUN_005cac50.

A case: id difficulty coldeffect res pierce level synergy kind bcol damage
(kind as freeze_rules.py: none, champion, unique, superunique, minion; bcol
the MonStats boss column; synergy the hard points in Shiver + Chilling Armor;
damage the striker's record +8 in points, 0 = the blow did none). Output
`id chill freeze` (ticks to the end of state 0xb / state 1, or `-`).
"""
import os
import random
import sys

sys.path.insert(0, os.getcwd())
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import freeze_rules  # noqa: E402

SKILL = 40
RECORD = 0x23C
KINDS = ("plain", "champion", "unique", "superunique", "minion")


def frames(level, synergy):
    """Frozen Armor calc1, ln56 x (100 + (Shiver + Chilling blvl) x par8) / 100; par5 30, par6 3, par8 5 (Skills.txt)."""
    return (30 + 3 * (level - 1)) * (100 + synergy * 5) // 100


def gen(n, seed):
    rows = []
    reses = (-100, -50, -1, 0, 1, 20, 33, 50, 75, 99, 100, 101, 150)
    for diff in range(3):
        for kind in range(5):
            for bcol in (0, 1):
                for ce in (-100, -50, -1, 0, 1, 25):
                    for res in reses:
                        for level in (1, 2, 5, 10, 20, 30):
                            for syn in (0, 1, 7, 40):
                                rows.append((diff, ce, res, 0, level, syn, kind, bcol, 1))
        for kind in range(5):
            for res in reses:
                for pierce in (1, 20, 50, 99, 100, 101):
                    for level in (1, 20):
                        rows.append((diff, -50, res, pierce, level, 0, kind, 0, 1))
        for level in (1, 20):
            rows.append((diff, -50, 0, 0, level, 0, 0, 0, 0))
    rng = random.Random(seed & 0xFFFFFFFF)
    for _ in range(n):
        rows.append((rng.randrange(3), rng.choice((-50, -33, -25, -100, 0, rng.randint(-128, 127))),
                     rng.choice((0, rng.randint(-100, 150))), rng.choice((0, 0, rng.randint(0, 120))),
                     rng.randint(1, 40), rng.choice((0, rng.randint(0, 60))), rng.choice((0, 0, 0, 1, 2, 3, 4)),
                     int(rng.random() < 0.1), rng.choice((1, 1, 1, 0))))
    for i, r in enumerate(rows):
        print(i, *r, sep="\t")


class Armor(freeze_rules.Game):
    def __init__(self):
        super().__init__()
        e = self.e
        self.frames = 0
        tables = e.r32(0x744304)
        self.skills = e.alloc(RECORD * 0x80)
        e.w32(tables + 0xB98, self.skills)
        e.w32(tables + 0xBA0, 0x80)
        e.w32(tables + 0xBC0, 0)
        row = self.skills + SKILL * RECORD
        e.w32(row + 0x138, 0x1234)
        e.mu.mem_write(row + 0x110, b"\xff\xff")
        self.calc_args = []
        e.hook(0x646CA0, self.calc, 4)
        e.hook(0x621E40, lambda h: 0, 3)
        self.hit = e.alloc(0x80)

    def calc(self, h):
        self.calc_args.append((h.arg(1), h.arg(2), h.arg(3)))
        return self.frames

    def armor(self, cid, diff, ce, res, pierce, level, syn, kind, bcol, damage):
        """freeze_rules.Game.run with the call of FUN_0057c6c0 replaced by one of FUN_005cac50."""
        e = self.e
        self.frames = frames(level, syn)
        orig = e.call

        def call_cac50(addr, defender, flag, record, ecx, edx):
            e.mu.mem_write(self.hit, b"\0" * 0x80)
            e.w32(self.hit + 8, damage << 8)
            return orig(0x5CAC50, edx, defender, self.hit, SKILL, level, ecx=ecx)
        e.call = call_cac50
        try:
            return self.run(cid, diff, ce, res, pierce, 0, 0, 0, 0, freeze_rules.KIND_FLAGS[kind], bcol)
        finally:
            e.call = orig


def cases(path):
    for line in open(path):
        f = line.split()
        if f:
            yield list(map(int, f))


def game(path):
    g = Armor()
    for cid, diff, ce, res, pierce, level, syn, kind, bcol, damage in cases(path):
        print(cid, *g.armor(cid, diff, ce, res, pierce, level, syn, kind, bcol, damage))
    bad = [a for a in g.calc_args if a[0] != 0x1234 or a[1] != SKILL]
    print(f"# calc calls {len(g.calc_args)}, not calc1 of skill {SKILL}: {len(bad)}", file=sys.stderr)


def attacker_class(r):
    _cid, _diff, _ce, res, _pierce, _level, _syn, kind, bcol, _damage = r
    who = "boss column" if bcol else KINDS[kind]
    cold = "immune" if res >= 100 else "resistant" if res > 0 else "no resistance"
    return who, cold


def compare(case_path, game_path, d2d_path):
    rows = {r[0]: r for r in cases(case_path)}

    def read(path, width):
        return {int(f[0]): f[1:width] for f in (line.split() for line in open(path)) if f and f[0].isdigit()}
    a, b = read(game_path, 3), read(d2d_path, 5)
    missing = [k for k in rows if k not in a or k not in b]
    bad = [k for k in rows if k in a and k in b and a[k] != b[k][:2]]
    frames_bad = [k for k in rows if k in b and int(b[k][2]) != frames(rows[k][5], rows[k][6])]
    resfail = [k for k in rows if k in b and "RESFAIL" in b[k]]
    print(f"cases {len(rows)} game {len(a)} d2d {len(b)} missing {len(missing)} mismatches {len(bad)} calc1 differs {len(frames_bad)} RESFAIL {len(resfail)}")
    for axis in (0, 1):
        per = {}
        for k in rows:
            per.setdefault(attacker_class(rows[k])[axis], [0, 0])[0] += 1
        for k in bad:
            per[attacker_class(rows[k])[axis]][1] += 1
        for name, (total, wrong) in sorted(per.items()):
            print(f"  {name:14s} {wrong:7d} of {total:7d}")
    for k in bad[:4]:
        print("  e.g.", *rows[k], "game", a[k], "d2d", b[k][:2])
    return 1 if bad or missing or frames_bad or resfail else 0


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "gen" and len(sys.argv) <= 4:
        gen(int(sys.argv[2]) if len(sys.argv) > 2 else 50000, int(sys.argv[3], 0) if len(sys.argv) > 3 else 1)
    elif cmd == "game" and len(sys.argv) == 3:
        game(sys.argv[2])
    elif cmd == "compare" and len(sys.argv) == 5:
        sys.exit(compare(*sys.argv[2:5]))
    else:
        sys.exit(__doc__)
