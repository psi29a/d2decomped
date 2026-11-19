# SPDX-License-Identifier: GPL-3.0-or-later
"""A summon's level, defense and attack rating in game.exe as the oracle: FUN_005c49e0, which the summoning dos run on
their new pet before its stats (FUN_005c4470).

    uv run python summon_level.py gen 100000 1 > cases.txt     # `clvl forced bonus difficulty expansion`
    uv run python summon_level.py game < cases.txt > game.txt   # each case and `| level ac tohit`
    ../../build/tests/test_summon_stats --summon-level < cases.txt | grep ' | ' > d2d.txt
    uv run python summon_level.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: the owner's level (stat 0xc), the level the do forces (above 0: taken as it is) and the bonus it passes, the
game's difficulty (+0x6d, 2 at most) and whether it's an expansion game (+0x6a or +0x74). FUN_005c49e0(ECX game,
EDX owner; pet, forced, bonus) runs for real: a forced level of 0 or below becomes min(max(3 x clvl / 4 + bonus, 1),
clvl); that level is set as the pet's stat 0xc (FUN_00627260); then the MonLvl row of that level (+0xb70, 0x78 bytes,
the last row at most) gives the defense (stat 0x1f) and attack rating (0x13) it adds (FUN_006272b0) for the
difficulty, the expansion columns (L-AC, L-TH) in an expansion game.
The callers' arguments (V read): Raise (FUN_005c4b00 0x5c4c5e), the golems (FUN_005c50c0 0x5c50cc), the Decoy
(FUN_005dc000 0x5dc149) and the Valkyrie (FUN_005dc1e0 0x5dc2c9) pass forced 0 and the skill level as the bonus; the
Druid's summons (FUN_005c7390 0x5c74c9..0x5c74de) and Raven (FUN_005c6910 0x5c6a29..0x5c6a3e) force max(calc2, 1);
the traps (FUN_005d5e10 0x5d5fc1) pass their own fifth argument (not traced here).
Each line: the case, then `level ac tohit` (the level set, the defense and attack rating added).
Hooked (inputs and side effects, not the claim): the stat getter FUN_00625480 (the owner's 0xc), the setter
FUN_00627260 and the adder FUN_006272b0 (recorded).
"""
import random
import sys

USAGE = "usage: summon_level.py gen N SEED | game < CASES | compare GAME D2D"


def boot():
    import drlg
    return drlg.boot()


class Rig:
    def __init__(self, e):
        self.e = e
        self.game, self.owner, self.pet = e.alloc(0x200), e.alloc(0x200), e.alloc(0x200)
        self.clvl, self.sets, self.adds = 1, {}, {}
        hooks = ((0x625480, lambda e: self.clvl if e.arg(0) == self.owner and e.arg(1) == 0xc else 0, 3),
                 (0x627260, self.set_stat, 4), (0x6272b0, self.add_stat, 4))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)

    def set_stat(self, e):
        self.sets[e.arg(1)] = e.arg(2)
        return 0

    def add_stat(self, e):
        self.adds[e.arg(1)] = self.adds.get(e.arg(1), 0) + e.arg(2)
        return 0

    def run(self, clvl, forced, bonus, difficulty, expansion):
        e = self.e
        self.clvl, self.sets, self.adds = clvl, {}, {}
        e.mu.mem_write(self.game + 0x6a, bytes([1 if expansion else 0]))
        e.w32(self.game + 0x74, 0)
        e.mu.mem_write(self.game + 0x6d, bytes([difficulty]))
        e.call(0x5c49e0, self.pet, forced & 0xffffffff, bonus & 0xffffffff, ecx=self.game, edx=self.owner)
        signed = lambda v: v - (1 << 32) if v & 0x80000000 else v
        return f"{signed(self.sets.get(0xc, 0))} {signed(self.adds.get(0x1f, 0))} {signed(self.adds.get(0x13, 0))}"


def gen(count, seed):
    rng = random.Random(seed)
    for clvl in range(1, 100):                                         # every owner level with the edges
        for forced, bonus in ((0, 0), (0, 1), (0, 20), (0, 99), (1, 0), (30, 5)):
            for difficulty in range(3):
                print(clvl, forced, bonus, difficulty, 1)
    for _ in range(count):
        clvl = rng.randint(1, 99)
        forced = rng.choice((0, 0, 0, rng.randint(1, 60), rng.randint(-5, 0)))
        bonus = rng.choice((0, rng.randint(1, 20), rng.randint(1, 60), rng.randint(-10, 0)))
        print(clvl, forced, bonus, rng.randint(0, 2), 1)                  # an expansion game: d2d keeps the L- columns


def game():
    rig = Rig(boot())
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        print(line.strip(), "|", rig.run(*(int(f) for f in field[:5])))


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
