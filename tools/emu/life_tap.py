# SPDX-License-Identifier: GPL-3.0-or-later
"""Life Tap's heal in game.exe as the oracle: its aura event function (FUN_005c61e0, slot 5 of 0x7325b0), which
FUN_0057c6c0 raises on the cursed unit as a hit lands (damagedinmelee / damagedbymissile, 0x57c7f3), after
FUN_0057c1e0 has put the hit's final damage at +8 and before the unit's life goes down.

    uv run python life_tap.py gen 100000 1 > cases.txt     # `striker life max damage pct`
    uv run python life_tap.py game < cases.txt > game.txt   # each case and `| life`
    ../../build/tests/test_life_tap --life-tap < cases.txt | grep ' | ' > d2d.txt
    uv run python life_tap.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: the striker's unit type (0 a player, 1 a monster: a merc or a pet is a monster), its life (stat 6) and max
life (FUN_00625d10), both in 256ths, the hit's damage (+8, 256ths) and what Life Tap's calc1 gives for the curse's
caster. FUN_005c61e0(ECX game; the cursed unit, the striker, the hit, skill 82, level 1) runs for real: the heal is
FUN_00483360(ECX the damage, EDX the %, 100) (0x5c62d7, real); with the striker's life and max both above 0 its life
becomes life + heal held between 1 and the max, set as stat 6 (FUN_00627260). No heal on a hit of 0 damage or less.
Each line: the case, then the striker's life after it (the case's life when nothing is set).
Hooked (inputs and side effects, not the claim): FUN_005541b0 (the striker alive), the curse's state on the cursed
unit FUN_00639df0 (present) and its stat list FUN_006256b0, the caster's lookup (FUN_006252b0, FUN_00625250,
FUN_00552f60), Life Tap's calc1 FUN_00646ca0 (the case's), the life getter FUN_00625480, the max FUN_00625d10, the
setter FUN_00627260 (recorded) and the overlay FUN_00621e40.
"""
import random
import sys

USAGE = "usage: life_tap.py gen N SEED | game < CASES | compare GAME D2D"
LIFE_TAP = 82


def boot():
    import drlg
    return drlg.boot()


class Rig:
    def __init__(self, e):
        self.e = e
        self.game, self.cursed, self.striker, self.hit = e.alloc(0x200), e.alloc(0x200), e.alloc(0x200), e.alloc(0x80)
        self.life, self.max_life, self.pct, self.sets = 0, 0, 0, {}
        hooks = ((0x5541b0, lambda e: 0, 0), (0x639df0, lambda e: 1, 2), (0x6256b0, lambda e: self.cursed + 0x100, 2),
                 (0x6252b0, lambda e: 0, 1), (0x625250, lambda e: 0, 1), (0x552f60, lambda e: self.striker, 1),
                 (0x646ca0, lambda e: self.pct & 0xffffffff, 4), (0x625480, self.get_stat, 3),
                 (0x625d10, lambda e: self.max_life & 0xffffffff, 1), (0x627260, self.set_stat, 4), (0x621e40, lambda e: 0, 3))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)

    def get_stat(self, e):
        return self.life & 0xffffffff if e.arg(0) == self.striker and e.arg(1) == 6 else 0

    def set_stat(self, e):
        if e.arg(0) == self.striker:
            self.sets[e.arg(1)] = e.arg(2)
        return 0

    def run(self, striker, life, max_life, damage, pct):
        e = self.e
        self.life, self.max_life, self.pct, self.sets = life, max_life, pct, {}
        e.w32(self.cursed, 1)
        e.w32(self.striker, striker)
        e.mu.mem_write(self.hit, bytes(0x80))
        e.w32(self.hit + 8, damage & 0xffffffff)
        e.call(0x5c61e0, self.cursed, self.striker, self.hit, LIFE_TAP, 1, ecx=self.game)
        signed = lambda v: v - (1 << 32) if v & 0x80000000 else v
        return f"{signed(self.sets.get(6, life & 0xffffffff))}"


def gen(count, seed):
    rng = random.Random(seed)
    for striker in (0, 1):
        for life, max_life in ((256, 256), (100 * 256, 400 * 256), (399 * 256 + 17, 400 * 256), (0, 400 * 256), (5000 * 256, 4000 * 256), (-256, 100), (300, 0)):
            for damage in (0, -5, 1, 255, 256, 0x10000, 0x10001, 0x100000, 0x100001, 50000 * 256):
                for pct in (0, 1, 7, 10, 15, 100, 0x10001, -20):
                    print(striker, life, max_life, damage, pct)
    for _ in range(count):
        max_life = rng.choice((rng.randint(1, 4000 * 256), rng.randint(1, 40000 * 256), rng.randint(-100, 300)))
        life = rng.choice((rng.randint(1, max(max_life, 1)), max_life, rng.randint(-300, 300), rng.randint(1, 50000 * 256)))
        damage = rng.choice((rng.randint(1, 3000 * 256), rng.randint(1, 30000 * 256), rng.randint(-300, 300), rng.randint(1, 0x7fffffff)))
        pct = rng.choice((rng.randint(5, 15), rng.randint(0, 100), rng.randint(-100, 0x20000)))
        print(rng.randint(0, 1), life, max_life, damage, pct)


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
