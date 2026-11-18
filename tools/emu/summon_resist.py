# SPDX-License-Identifier: GPL-3.0-or-later
"""Summon Resist on a summon in game.exe as the oracle: FUN_005c40d0, which a raised skeleton or mage (Raise's do
FUN_005c4b00, 0x5c4c97) and a golem (FUN_005c50c0) get after their stats.

    uv run python summon_resist.py gen 100000 1 > cases.txt     # `value absorb_fire absorb_light absorb_cold`
    uv run python summon_resist.py game < cases.txt > game.txt   # each case and `| stat=value ..`
    ../../build/tests/test_summon_stats --summon-resist < cases.txt | grep ' | ' > d2d.txt
    uv run python summon_resist.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: the owner's stat 0x15d (passive_summon_resist: Summon Resist's passivestat) and the pet's stats 0x8e, 0x90
and 0x94 (its fire, lightning and cold absorb percent). FUN_005c40d0(ECX owner, EDX pet) runs for real: a non-zero
0x15d puts one state node on the pet (FUN_006251f0, FUN_00626e10) with that value as fire resist 0x27 unless 0x8e
is above 0, lightning 0x29 unless 0x90 is, cold 0x2b unless 0x94 is, and poison 0x2d (FUN_00627150).
Each line: the case, then the node's stats (`stat=value`, as added; nothing when no node was made).
Hooked (inputs and side effects, not the claim): the stat getter FUN_00625480 (the case's values), the node
FUN_006251f0 / FUN_00626e10 and its adds FUN_00627150 (recorded).
"""
import random
import sys

USAGE = "usage: summon_resist.py gen N SEED | game < CASES | compare GAME D2D"


def boot():
    import drlg
    return drlg.boot()


class Rig:
    def __init__(self, e):
        self.e = e
        self.owner, self.pet = e.alloc(0x200), e.alloc(0x200)
        e.w32(self.pet, 1)
        self.stats, self.adds = {}, []
        hooks = ((0x625480, self.stat, 3), (0x6251f0, lambda e: e.alloc(0x40), 5), (0x626e10, lambda _e: 0, 3),
                 (0x627150, self.add, 4))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)

    def stat(self, e):
        return self.stats.get((e.arg(0), e.arg(1)), 0) & 0xffffffff

    def add(self, e):
        value = e.arg(2)
        self.adds.append((e.arg(1), value - (1 << 32) if value & 0x80000000 else value))
        return 0

    def run(self, value, fire, light, cold):
        self.stats = {(self.owner, 0x15d): value, (self.pet, 0x8e): fire, (self.pet, 0x90): light, (self.pet, 0x94): cold}
        self.adds = []
        self.e.call(0x5c40d0, ecx=self.owner, edx=self.pet)
        return " ".join(f"{stat}={value}" for stat, value in self.adds)


def gen(count, seed):
    rng = random.Random(seed)
    for value in (0, 1, -1, 5, 75):                                    # every absorb pattern at a few values
        for mask in range(8):
            print(value, *(rng.choice((1, 40)) if mask >> k & 1 else 0 for k in range(3)))
    for _ in range(count):
        value = rng.choice((0, rng.randint(1, 100), rng.randint(-100, 300)))
        print(value, *(rng.choice((0, 0, 1, -5, rng.randint(1, 100))) for _ in range(3)))


def game():
    rig = Rig(boot())
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        print(line.strip(), "|", rig.run(*(int(f) for f in field[:4])))


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
