# SPDX-License-Identifier: GPL-3.0-or-later
"""A revived monster's life and level in game.exe as the oracle: Revive's do (FUN_005c56c0, do 58), before the summon
stats (FUN_005c4470) it then runs.

    uv run python revive_life.py gen 100000 1 > cases.txt     # `type difficulty level clvl seed_low seed_high`
    uv run python revive_life.py game < cases.txt > game.txt   # each case and `| life level`
    ../../build/tests/test_summon_stats --revive-life < cases.txt | grep ' | ' > d2d.txt
    uv run python revive_life.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: the corpse's MonStats row (unit +4), the game's difficulty (+0x6d), the corpse's level (its stat 0xc), the
owner's level (stat 0xc) and the corpse's unit seed (+0x20, +0x24). FUN_005c56c0(ECX game, EDX owner; skill, level)
runs for real from the skill record to its call of FUN_005c4470: FUN_006538a0 (real) gives the row's life range at
the corpse's level, FUN_0045c3e0 (real) rolls min + rand(max - min + 1) on the seed, in 256ths it is set as stats 7
and 6 (FUN_00627260); when the corpse's level is above the owner's, that life becomes MulDiv(life, clvl, level)
(FUN_00483360, real), 1 at least, and the level the owner's.
Each line: the case, then `life level`: the last stat 7 set (256ths) and the level the pet ends at (stat 0xc as set,
else the corpse's).
Hooked (inputs and the rest of the do, not the claim): the unit by id FUN_00553540 (the corpse), the corpse test
FUN_005c3300 (passes), the Paladin's hit FUN_005c2ff0, FUN_005c5430, the stat getter FUN_00625480 (both levels), the
setter FUN_00627260 (recorded) and the calls after the life (FUN_005c4470, FUN_005c55c0, FUN_00646ca0, FUN_00575d90,
FUN_005b1900, FUN_00649ca0). Expansion columns only (d2d keeps the L- ones).
"""
import random
import sys

USAGE = "usage: revive_life.py gen N SEED | game < CASES | compare GAME D2D"
REVIVE = 95


def boot():
    import drlg
    return drlg.boot()


class Rig:
    def __init__(self, e):
        self.e = e
        self.game, self.owner, self.pet = e.alloc(0x200), e.alloc(0x200), e.alloc(0x200)
        self.levels, self.sets = {}, {}
        hooks = ((0x553540, lambda e: self.pet, 2), (0x5c3300, lambda e: 1, 0), (0x5c2ff0, lambda e: 0, 2),
                 (0x5c5430, lambda e: 0, 1), (0x625480, self.get_stat, 3), (0x627260, self.set_stat, 4),
                 (0x5c4470, lambda e: 0, 4), (0x5c55c0, lambda e: 0, 3), (0x646ca0, lambda e: 0, 4),
                 (0x575d90, lambda e: 0, 3), (0x5b1900, lambda e: 0, 2), (0x649ca0, lambda e: 0, 1))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)

    def get_stat(self, e):
        return self.levels.get(e.arg(0), 0) if e.arg(1) == 0xc else 0

    def set_stat(self, e):
        if e.arg(0) == self.pet:
            self.sets[e.arg(1)] = e.arg(2)
        return 0

    def run(self, row, difficulty, level, clvl, seed_low, seed_high):
        e = self.e
        self.levels, self.sets = {self.pet: level, self.owner: clvl}, {}
        e.w32(self.pet, 1)
        e.w32(self.pet + 4, row)
        e.w32(self.pet + 0x20, seed_low)
        e.w32(self.pet + 0x24, seed_high)
        e.mu.mem_write(self.game + 0x6a, bytes([1]))
        e.w32(self.game + 0x74, 0)
        e.mu.mem_write(self.game + 0x6d, bytes([difficulty]))
        e.call(0x5c56c0, REVIVE, 1, ecx=self.game, edx=self.owner)
        signed = lambda v: v - (1 << 32) if v & 0x80000000 else v
        return f"{signed(self.sets.get(7, 0))} {signed(self.sets.get(0xc, level))}"


def gen(count, seed):
    rng = random.Random(seed)
    rows = 734                                                          # MonStats rows in 1.14d ([0x744304] +0xa80)
    for level, clvl in ((1, 1), (0, 5), (30, 29), (30, 30), (30, 31), (99, 1), (85, 99), (1, 99)):
        for difficulty in range(3):
            for row in (0, 5, 363, 440):
                print(row, difficulty, level, clvl, 0x1234 + level, 666)
    for _ in range(count):
        level = rng.randint(0, 99)
        clvl = rng.choice((rng.randint(1, 99), max(level - rng.randint(0, 5), 1)))
        print(rng.randrange(rows), rng.randint(0, 2), level, clvl, rng.getrandbits(32), rng.choice((666, rng.getrandbits(32))))


def game():
    rig = Rig(boot())
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        print(line.strip(), "|", rig.run(*(int(f) for f in field[:6])))


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
