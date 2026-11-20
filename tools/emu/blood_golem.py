# SPDX-License-Identifier: GPL-3.0-or-later
"""Blood Golem's life steal in game.exe as the oracle: its domeleedamage event function (FUN_005c6530, slot 23 of
0x7325b0), which FUN_0057c6c0 raises on the golem as its melee hit lands (event 5, 0x57c7e3), once the hit is filled
and before the struck unit's life goes down.

    uv run python blood_golem.py gen 100000 1 > cases.txt     # `target drain align phys tlife owner olife omax glife gmax lvl`
    uv run python blood_golem.py game < cases.txt > game.txt   # each case and `| owner_life golem_life`
    ../../build/tests/test_blood_golem --blood-golem < cases.txt | grep ' | ' > d2d.txt
    uv run python blood_golem.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: the struck unit's type (0 a player, 1 a monster), its MonStats Drain at the difficulty (a monster's), its
alignment (FUN_006259b0: 0 evil), the hit's physical (+8, 256ths), the struck unit's life (stat 6, 256ths), whether the
golem has an owner (FUN_0058f0d0), the owner's life and max, the golem's life and max (256ths), and Blood Golem's
level. FUN_005c6530(ECX game; golem, struck unit, hit, skill BloodGolem, level) runs for real: a monster's Drain
(+0xa0 + difficulty, over 0) cuts the physical to MulDiv(+8, Drain, 100) unless it is 100; it is held to the struck
unit's life; only an evil one (alignment 0) gives; the steal % is FUN_00645b70 (the dm of Param1 / Param2 at the
level, real), the life MulDiv(that, %, 100) (FUN_00483360, real); the owner first takes Param3 % of it
(FUN_004efc50, real), the golem the rest, the owner what the golem has no room for, each through FUN_005c5f10 (real:
up to its max, stat 6 set).
Each line: the case, then the owner's and the golem's life after it (the case's when nothing is set).
Hooked (inputs and side effects, not the claim): the MonStats record FUN_0055b7e0 (Drain), the alignment FUN_006259b0,
the stat getter FUN_00625480 (life), the max FUN_00625d10, the setter FUN_00627260 (recorded), the owner FUN_0058f0d0,
FUN_005541b0 (no one dying) and the overlay FUN_00621e40.
"""
import random
import sys

USAGE = "usage: blood_golem.py gen N SEED | game < CASES | compare GAME D2D"
BLOOD_GOLEM = 85


def boot():
    import drlg
    return drlg.boot()


class Rig:
    def __init__(self, e):
        self.e = e
        self.game, self.golem, self.target, self.owner = (e.alloc(0x200) for _ in range(4))
        self.hit, self.record = e.alloc(0x80), e.alloc(0x200)
        self.lives, self.maxes, self.align, self.has_owner = {}, {}, 0, True
        hooks = ((0x55b7e0, lambda e: self.record, 0), (0x6259b0, lambda e: self.align, 1), (0x625480, self.get_stat, 3),
                 (0x625d10, lambda e: self.maxes.get(e.arg(0), 0) & 0xffffffff, 1), (0x627260, self.set_stat, 4),
                 (0x58f0d0, lambda e: self.owner if self.has_owner else 0, 0), (0x5541b0, lambda e: 0, 0),
                 (0x621e40, lambda e: 0, 3))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)

    def get_stat(self, e):
        return self.lives.get(e.arg(0), 0) & 0xffffffff if e.arg(1) == 6 else 0

    def set_stat(self, e):
        if e.arg(1) == 6:
            value = e.arg(2)
            self.lives[e.arg(0)] = value - (1 << 32) if value & 0x80000000 else value
        return 0

    def run(self, target, drain, align, phys, target_life, has_owner, owner_life, owner_max, golem_life, golem_max, level):
        e = self.e
        self.align, self.has_owner = align, bool(has_owner)
        self.lives = {self.target: target_life, self.owner: owner_life, self.golem: golem_life}
        self.maxes = {self.owner: owner_max, self.golem: golem_max}
        e.w32(self.golem, 1)
        e.w32(self.target, target)
        e.mu.mem_write(self.record + 0xa0, bytes([drain & 0xff] * 3))
        e.mu.mem_write(self.game + 0x6d, bytes([0]))
        e.mu.mem_write(self.hit, bytes(0x80))
        e.w32(self.hit + 8, phys & 0xffffffff)
        e.call(0x5c6530, self.golem, self.target, self.hit, BLOOD_GOLEM, level & 0xffffffff, ecx=self.game)
        return f"{self.lives[self.owner]} {self.lives[self.golem]}"


def gen(count, seed):
    rng = random.Random(seed)
    for target, drain, align in ((1, 100, 0), (1, 50, 0), (1, 0, 0), (1, 100, 1), (0, 100, 0), (0, 100, 1), (1, 200, 0)):
        for phys, target_life in ((100 * 256, 5000 * 256), (100 * 256, 30 * 256), (0, 5000 * 256), (-256, 100), (255, 1000)):
            for has_owner, owner_life, owner_max in ((1, 100 * 256, 400 * 256), (1, 399 * 256, 400 * 256), (0, 0, 0), (1, 0, 400 * 256)):
                for golem_life, golem_max, level in ((50 * 256, 500 * 256, 1), (499 * 256, 500 * 256, 20), (500 * 256, 500 * 256, 0)):
                    print(target, drain, align, phys, target_life, has_owner, owner_life, owner_max, golem_life, golem_max, level)
    for _ in range(count):
        golem_max = rng.randint(1, 5000 * 256)
        owner_max = rng.randint(1, 3000 * 256)
        print(rng.choice((1, 1, 1, 0)), rng.choice((100, 100, rng.randint(0, 255))), rng.choice((0, 0, 0, 1, 2)),
              rng.choice((rng.randint(1, 500 * 256), rng.randint(1, 5000 * 256), rng.randint(-300, 300))),
              rng.choice((rng.randint(1, 50000 * 256), rng.randint(1, 100 * 256))), rng.choice((1, 1, 1, 0)),
              rng.choice((rng.randint(1, owner_max), owner_max, rng.randint(-300, 300))), owner_max,
              rng.choice((rng.randint(1, golem_max), golem_max)), golem_max, rng.choice((rng.randint(1, 30), rng.randint(0, 99), rng.randint(-2, 0))))


def game():
    rig = Rig(boot())
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        print(line.strip(), "|", rig.run(*(int(f) for f in field[:11])))


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
            if len(case) != 11 or len(result) != 2 or not all(field.lstrip("-").isdigit() for field in case + result):
                sys.exit(f"malformed line in {path}: {line.strip()!r} (11 case fields and 2 results, integers)")
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
