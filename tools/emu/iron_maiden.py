# SPDX-License-Identifier: GPL-3.0-or-later
"""Iron Maiden's reflected hit in game.exe as the oracle: its aura event function (FUN_005c5f50, slot 4 of 0x7325b0),
which FUN_0057c6c0 raises on the cursed unit as its own hit on another lands (domeleedamage, event 5).

    uv run python iron_maiden.py gen 100000 1 > cases.txt     # `wearer merc struck phys calc1 calc2 calc3`
    uv run python iron_maiden.py game < cases.txt > game.txt   # each case and `| damage result class`
    ../../build/tests/test_iron_maiden --iron-maiden < cases.txt | grep ' | ' > d2d.txt
    uv run python iron_maiden.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: the cursed unit's type (0 a player, 1 a monster), whether it is a hireling (FUN_0063ee90), the struck unit's
type, the physical its hit dealt (+8, 256ths) and what Iron Maiden's calc1 / calc2 / calc3 give for the curse's
caster. FUN_005c5f50(ECX game; the cursed unit, the struck unit, the hit, skill 76, level 1) runs for real: the
percent is calc1 for a monster that isn't a hireling, else calc2 when the struck unit is a player and calc3 when it
isn't; the reflected hit is a zeroed record whose +8 is FUN_00483360(ECX the hit's +8, EDX the percent, 100)
(0x5c607e, real), result flags (+4) the row's +0x12e | 0x20, hit flags (+0) the row's +0x130, class (+0x60) the row's
+0x134, struck on the cursed unit by FUN_0057c6c0(game, struck unit, cursed unit, 1, record), then FUN_0057cee0.
Each line: the case, then the record FUN_0057c6c0 gets: `damage result class` (none: `- - -`). The hit flags (+0) are
Iron Maiden's +0x130, 0 in 1.14d, and d2d has no field for them: not printed.
Hooked (inputs and side effects, not the claim): FUN_005541b0 (no one dying), the curse's state FUN_00639df0 /
FUN_006256b0, the caster's lookup (FUN_006252b0, FUN_00625250, FUN_00552f60), FUN_0063ee90 (hireling), the calcs
FUN_00646ca0 (the case's, by the calc the code reads: +0x138, +0x13c or +0x140), FUN_0057c6c0 (recorded) and
FUN_0057cee0. The struck unit's class is 0, so the leech tail for class 0x122 (0x5c60d6) doesn't run.
"""
import random
import sys

USAGE = "usage: iron_maiden.py gen N SEED | game < CASES | compare GAME D2D"
IRON_MAIDEN = 76


def boot():
    import drlg
    return drlg.boot()


class Rig:
    def __init__(self, e):
        self.e = e
        self.game, self.cursed, self.struck, self.hit = e.alloc(0x200), e.alloc(0x200), e.alloc(0x200), e.alloc(0x80)
        tables = e.r32(0x744304)
        self.row = e.r32(tables + 0xb98) + IRON_MAIDEN * 0x23c
        self.calc_ids = {e.r32(self.row + offset): index for index, offset in enumerate((0x138, 0x13c, 0x140))}
        self.merc, self.calcs, self.record = 0, (0, 0, 0), None
        hooks = ((0x5541b0, lambda e: 0, 0), (0x639df0, lambda e: 1, 2), (0x6256b0, lambda e: self.cursed + 0x100, 2),
                 (0x6252b0, lambda e: 0, 1), (0x625250, lambda e: 0, 1), (0x552f60, lambda e: self.game + 0x100, 1),
                 (0x63ee90, lambda e: self.merc, 1), (0x646ca0, self.calc, 4), (0x57c6c0, self.strike, 3),
                 (0x57cee0, lambda e: 0, 2))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)

    def calc(self, e):
        return self.calcs[self.calc_ids.get(e.arg(1), 0)] & 0xffffffff

    def strike(self, e):
        record = e.arg(2)
        self.record = (self.e.s32(record + 8), self.e.r32(record + 4) & 0xffff, self.e.s32(record + 0x60))
        return 0

    def run(self, wearer, merc, struck, phys, calc1, calc2, calc3):
        e = self.e
        self.merc, self.calcs, self.record = merc, (calc1, calc2, calc3), None
        e.w32(self.cursed, wearer)
        e.w32(self.struck, struck)
        e.w32(self.struck + 4, 0)
        e.mu.mem_write(self.hit, bytes(0x80))
        e.w32(self.hit + 8, phys & 0xffffffff)
        e.call(0x5c5f50, self.cursed, self.struck, self.hit, IRON_MAIDEN, 1, ecx=self.game)
        return " ".join(str(v) for v in self.record) if self.record else "- - -"


def gen(count, seed):
    rng = random.Random(seed)
    for wearer, merc, struck in ((1, 0, 0), (1, 0, 1), (1, 1, 0), (1, 1, 1), (0, 0, 0), (0, 0, 1)):
        for phys in (0, -5, 1, 255, 256, 0x10000, 0x10001, 0x100000, 0x100001, 50000 * 256):
            for calc1, calc2, calc3 in ((200, 100, 50), (0, 0, 0), (325, 15, 30), (0x10001, -20, 7)):
                print(wearer, merc, struck, phys, calc1, calc2, calc3)
    for _ in range(count):
        print(rng.choice((1, 1, 1, 0)), rng.choice((0, 0, 0, 1)), rng.choice((0, 1)),
              rng.choice((rng.randint(1, 500 * 256), rng.randint(1, 5000 * 256), rng.randint(-300, 300), rng.randint(1, 0x7fffffff))),
              rng.choice((rng.randint(200, 500), rng.randint(0, 0x20000))), rng.randint(-50, 200), rng.randint(-50, 200))


def game():
    rig = Rig(boot())
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        print(line.strip(), "|", rig.run(*(int(f) for f in field[:7])))


def is_int(token):
    try:
        int(token)
    except ValueError:
        return False
    return True


def rows(path):
    out = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            if "|" not in line:
                sys.exit(f"malformed line in {path}: {line.strip()!r}")
            case, result = line.split("|", 1)
            case, result = case.split(), result.split()
            if len(case) != 7 or len(result) != 3 or not all(is_int(f) for f in case) \
                    or not (result == ["-"] * 3 or all(is_int(f) for f in result)):
                sys.exit(f"malformed row in {path}: {line.strip()!r}")
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
