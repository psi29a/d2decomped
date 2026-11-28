# SPDX-License-Identifier: GPL-3.0-or-later
"""A missile record's crit and the caster's seed after, by game.exe 1.14d's own FUN_0064a850.

    uv run python missile_crit.py gen [N] [SEED] > cases.txt      # synthetic cases (default 6000)
    uv run python missile_crit.py game cases.txt > game.txt       # game.exe's answers (no game data)
    uv run python missile_crit.py inc cases.txt game.txt > ../../tests/missile_crit_cases.inc

tests/test_missile_crit.cpp runs rules::missile_crit on the same cases. A case:
`id critical deadly mastery weapon low high`: the caster's critical strike (stat
0x151), deadly strike (0x8d), its weapon mastery's crit (FUN_00645830's answer),
whether a weapon is passed (FUN_0064b860's SrcDamage branch passes the caster's,
the +0x12e branch none) and its seed (+0x20 / +0x24).

Game side: FUN_0064a850 (ESI the caster, the weapon on the stack) runs for real;
hooked (not the claim): d2d_unit_stat_value (0x625480) and FUN_00625500, the
caster's stats 0x151 / 0x8d from the case, and FUN_00645830, the mastery's
crit. Output `id | crit low high`.
"""
import random
import sys

WEAPON = 0x4B000000


def gen(n, seed):
    rng = random.Random(seed)
    chances = (0, 0, 0, 1, 3, 5, 10, 25, 50, 75, 99, 100, -5)
    for i in range(n):
        print(i, rng.choice(chances), rng.choice(chances), rng.choice(chances), int(rng.random() < 0.7),
              rng.getrandbits(32), rng.choice((666, rng.getrandbits(32))))


def read(path):
    for line in open(path):
        f = line.split()
        if f: yield f


def game(path):
    import emu
    e = emu.Emu()
    unit = e.alloc(0x100)
    case = {}
    stat = lambda m: case["critical"] if m.arg(1) == 0x151 else case["deadly"] if m.arg(1) == 0x8d else 0
    e.hook(0x625480, stat, 3)
    e.hook(0x625500, stat, 3)
    e.hook(0x645830, lambda m: case["mastery"], 4)
    for f in read(path):
        i, critical, deadly, mastery, weapon, low, high = (int(v) for v in f)
        case.update(critical=critical, deadly=deadly, mastery=mastery)
        e.w32(unit + 0x20, low); e.w32(unit + 0x24, high)
        crit = e.call(0x64a850, WEAPON if weapon else 0, regs={"esi": unit})
        print(i, "|", int(crit != 0), e.r32(unit + 0x20), e.r32(unit + 0x24))


def inc(cases_path, game_path):
    answers = {f[0]: f[2:] for f in read(game_path)}
    print("// tools/emu/missile_crit.py inc: game.exe 1.14d's FUN_0064a850 on each case (see the script)")
    print("// {critical, deadly, mastery, weapon, low, high, crit, low after, high after}")
    for f in read(cases_path):
        crit, low, high = answers[f[0]]
        print(f"{{ {f[1]}, {f[2]}, {f[3]}, {f[4]}, {f[5]}u, {f[6]}u, {crit}, {low}u, {high}u }},")


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    if mode == "gen": gen(int(sys.argv[2]) if len(sys.argv) > 2 else 6000, int(sys.argv[3]) if len(sys.argv) > 3 else 20261127)
    elif mode == "game": game(sys.argv[2])
    elif mode == "inc": inc(sys.argv[2], sys.argv[3])
    else: sys.exit(__doc__)
