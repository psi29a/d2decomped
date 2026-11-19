# SPDX-License-Identifier: GPL-3.0-or-later
"""Corpse Explosion's blow in game.exe as the oracle: its do (FUN_005c4df0, do 55), up to the walk (FUN_0056b7e0) that
hits what's round the corpse.

    uv run python corpse_explosion.py gen 100000 1 > cases.txt     # `type difficulty level clvl seed_low seed_high calc1 calc2 calc3 range`
    uv run python corpse_explosion.py game < cases.txt > game.txt   # each case and `| physical element walk reach2`
    ../../build/tests/test_corpse_explosion --corpse-explosion < cases.txt | grep ' | ' > d2d.txt
    uv run python corpse_explosion.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: the corpse's MonStats row (unit +4), the game's difficulty (+0x6d), the corpse's level (its stat 0xc), the
caster's level (stat 0xc), the corpse's unit seed (+0x20, +0x24), and what the caster's calcs (FUN_00646ca0) give for
Corpse Explosion's calc1, calc2, calc3 and aurarange. FUN_005c4df0(ECX game, EDX caster; skill 74, level 1) runs for
real: the corpse's life, (min + max) << 7 of FUN_006538a0's range at its level (real); calc1 % and calc2 % of it
(FUN_00483360, real, ECX the % and EDX the life: 0x5c4f31, 0x5c4f57); the roll between them on the seed
(FUN_0045c3e0, real); above the caster's level, x clvl / its level; calc3 % (0..100) of it to the skill's EType
(+0x1dc) through FUN_0056c8e0 and the rest physical; the walk's radius (range + 1) / 2 and the physical's reach
(range / 2) squared.
Each line: the case, then `physical element walk reach2`: the blow's physical (256ths, +8 of the damage block the walk
gets), the element's (FUN_0056c8e0's second argument, 0 when it isn't called), the walk's radius and the squared reach.
Hooked (inputs and the rest of the do, not the claim): the unit by id FUN_00553540 (the corpse), the corpse test
FUN_00645680 (passes), FUN_00639db0, FUN_0064c040, the stat getter FUN_00625480 (both levels), the calcs FUN_00646ca0
(the case's, by call order: aurarange, calc1, calc2, calc3), FUN_00644f20 (the element's length), the position
getters FUN_0045adf0 / FUN_0045ae20, FUN_0056c8e0 and FUN_0056b7e0 (recorded). Expansion columns only (d2d keeps
the L- ones); a monster corpse only (unit type 1; a player's, FUN_006253b0's max life, isn't in d2d).
"""
import random
import sys

USAGE = "usage: corpse_explosion.py gen N SEED | game < CASES | compare GAME D2D"
CORPSE_EXPLOSION = 74
ROWS = 734                                                              # MonStats rows in 1.14d ([0x744304] +0xa80)


def boot():
    import drlg
    return drlg.boot()


class Rig:
    def __init__(self, e):
        self.e = e
        self.game, self.owner, self.corpse = e.alloc(0x200), e.alloc(0x200), e.alloc(0x200)
        self.levels, self.calcs, self.calls, self.element, self.walk = {}, [], 0, 0, None
        hooks = ((0x553540, lambda e: self.corpse, 2), (0x645680, lambda e: 1, 1), (0x639db0, lambda e: 0, 3),
                 (0x64c040, lambda e: 0, 1), (0x625480, self.get_stat, 3), (0x646ca0, self.calc, 4),
                 (0x644f20, lambda e: 0, 4), (0x45adf0, lambda e: 0, 0), (0x45ae20, lambda e: 0, 0),
                 (0x56c8e0, self.add_element, 5), (0x56b7e0, self.walk_round, 9))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)

    def get_stat(self, e):
        return self.levels.get(e.arg(0), 0) if e.arg(1) == 0xc else 0

    def calc(self, e):
        value = self.calcs[self.calls] if self.calls < len(self.calcs) else 0
        self.calls += 1
        return value & 0xffffffff

    def add_element(self, e):
        self.element = e.arg(1)
        return 0

    def walk_round(self, e):
        context = e.arg(5)
        self.walk = (e.arg(2), self.e.r32(self.e.r32(context) + 8), self.e.r32(context + 0xc))
        return 0

    def run(self, row, difficulty, level, clvl, seed_low, seed_high, calc1, calc2, calc3, aura_range):
        e = self.e
        self.levels, self.calls, self.element, self.walk = {self.corpse: level, self.owner: clvl}, 0, 0, None
        self.calcs = [aura_range, calc1, calc2, calc3]
        e.w32(self.corpse, 1)
        e.w32(self.corpse + 4, row)
        e.w32(self.corpse + 0x20, seed_low)
        e.w32(self.corpse + 0x24, seed_high)
        e.mu.mem_write(self.game + 0x6a, bytes([1]))
        e.w32(self.game + 0x74, 0)
        e.mu.mem_write(self.game + 0x6d, bytes([difficulty]))
        e.call(0x5c4df0, CORPSE_EXPLOSION, 1, ecx=self.game, edx=self.owner)
        signed = lambda v: v - (1 << 32) if v & 0x80000000 else v
        walk, physical, reach2 = self.walk if self.walk else (0, 0, 0)
        return f"{signed(physical)} {signed(self.element)} {signed(walk)} {signed(reach2)}"


def gen(count, seed):
    rng = random.Random(seed)
    for level, clvl in ((1, 1), (0, 5), (30, 29), (30, 30), (85, 99), (99, 1)):
        for difficulty in range(3):
            for row in (0, 5, 156, 440):
                for calc1, calc2, calc3, aura_range in ((60, 80, 50, 7), (70, 70, 0, 6), (0, 0, 100, 1), (110, 180, 150, 13)):
                    print(row, difficulty, level, clvl, 0x1234 + level, 666, calc1, calc2, calc3, aura_range)
    for _ in range(count):
        level = rng.randint(0, 99)
        clvl = rng.choice((rng.randint(1, 99), max(level - rng.randint(0, 5), 1)))
        calc1 = rng.randint(0, 200)
        calc2 = rng.choice((calc1, calc1 + rng.randint(0, 100), rng.randint(0, 200)))
        print(rng.randrange(ROWS), rng.randint(0, 2), level, clvl, rng.getrandbits(32), rng.choice((666, rng.getrandbits(32))),
              calc1, calc2, rng.choice((0, 50, rng.randint(-20, 130))), rng.randint(0, 30))


def game():
    rig = Rig(boot())
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        print(line.strip(), "|", rig.run(*(int(f) for f in field[:10])))


def rows(path):
    out = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            if "|" not in line:
                sys.exit(f"malformed line in {path}: {line.strip()!r}")
            case, result = line.split("|", 1)
            out.append((case.split(), result.split()))
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
