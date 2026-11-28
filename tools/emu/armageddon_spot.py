# SPDX-License-Identifier: GPL-3.0-or-later
"""Armageddon's pulse, by game.exe 1.14d's own FUN_005c8520 (its state's timer event).

    uv run python armageddon_spot.py gen [N] [SEED] > cases.txt   # synthetic cases (default 4000)
    uv run python armageddon_spot.py game cases.txt > game.txt    # game.exe's answers (no game data)
    uv run python armageddon_spot.py inc cases.txt game.txt > ../../tests/armageddon_spot_cases.inc

tests/test_armageddon_spot.cpp runs rules::armageddon_spot on the same cases. A case:
`id word radius low high refused`: the skill record's +0x18 (rand(0x10000) on the
caster's seed at the cast, FUN_005c8190; then each pulse's), the radius (aurarange),
the caster's seed and how many of the tries are refused before one is taken (5: all).

Game side: a synthetic table block at 0x744304 (one Skills.txt row of 0x23c, aurastate
+0x80 0, flags +5 0; a States count at +0xc4), FUN_005c8520 runs for real on a caster
(ECX the game, EDX the caster, the skill 0 and level 1), the record's +0x18 read and
written by FUN_006444a0 / FUN_00644560. Hooked (not the claim): the state FUN_00639df0
(on), the caster's room FUN_00620bb0, town FUN_0061ab00 (no), the events FUN_00540e60
/ FUN_005417d0 / FUN_004efc80, the missile row FUN_005d3cf0, the caster's skill record
FUN_006439f0, aurarange FUN_00646ca0 (the case's), the subtile FUN_0045adf0 /
FUN_0045ae20 (5000, 6000), the line FUN_00645950 (the case's first `refused` tries
fail), the room FUN_00463740 and the collision FUN_0064cb30 (free), the missile maker
FUN_0056ede0 (its x / y recorded) and FUN_00571aa0. Output `id | dx dy word low high`
(dx dy "-" for none).
"""
import random
import sys

CELL_X, CELL_Y = 5000, 6000


def gen(n, seed):
    rng = random.Random(seed)
    for i in range(n):
        word = rng.choice((0, 1, rng.randrange(0x10000), rng.randrange(1000000), rng.getrandbits(32)))
        radius = rng.choice((1, 2, 3, 5, 8, 12, rng.randrange(1, 30)))
        refused = rng.choice((0, 0, 0, 1, 2, 4, 5))
        print(i, word, radius, rng.getrandbits(32), rng.choice((666, rng.getrandbits(32))), refused)


def read(path):
    for line in open(path):
        f = line.split()
        if f: yield f


def game(path):
    import emu
    e = emu.Emu()
    tables, skills, game_unit = e.alloc(0x1000), e.alloc(0x23c), e.alloc(0x200)
    e.w32(0x744304, tables)
    e.w32(tables + 0xb98, skills); e.w32(tables + 0xba0, 1); e.w32(tables + 0xc4, 8)
    record, caster = e.alloc(0x40), e.alloc(0x200)
    case = {}
    made = []

    def line(m):
        case["tries"] += 1
        return int(case["tries"] > case["refused"])

    def on_make(m):
        made.append((m.arg(3), m.arg(4)))
        return 1
    e.hook(0x639df0, lambda m: 1, 2)
    e.hook(0x620bb0, lambda m: 0x3000, 1)
    e.hook(0x61ab00, lambda m: 0, 1)
    e.hook(0x540e60, lambda m: 0, 1)
    e.hook(0x5417d0, lambda m: 0, 4)
    e.hook(0x4efc80, lambda m: 0, 0)
    e.hook(0x5d3cf0, lambda m: 1, 0)
    e.hook(0x6439f0, lambda m: record, 0)
    e.hook(0x646ca0, lambda m: case["radius"], 4)
    e.hook(0x45adf0, lambda m: CELL_X, 0)
    e.hook(0x45ae20, lambda m: CELL_Y, 0)
    e.hook(0x645950, line, 4)
    e.hook(0x463740, lambda m: 0x3000, 1)
    e.hook(0x64cb30, lambda m: 0, 4)
    e.hook(0x56ede0, on_make, 5)
    e.hook(0x571aa0, lambda m: 0, 5)
    for f in read(path):
        i, word, radius, low, high, refused = (int(v) for v in f)
        case.update(radius=radius, refused=refused, tries=0)
        e.mu.mem_write(caster, b"\0" * 0x200)
        e.w32(caster + 0x20, low); e.w32(caster + 0x24, high)
        e.w32(record + 0x18, word)
        made.clear()
        e.call(0x5c8520, 0, 1, ecx=game_unit, edx=caster)
        dx, dy = (made[0][0] - CELL_X, made[0][1] - CELL_Y) if made else ("-", "-")
        print(i, "|", dx, dy, e.r32(record + 0x18), e.r32(caster + 0x20), e.r32(caster + 0x24))


def inc(cases_path, game_path):
    answers = {f[0]: f[2:] for f in read(game_path)}
    print("// tools/emu/armageddon_spot.py inc: game.exe 1.14d's FUN_005c8520 on each case (see the script)")
    print("// {word, radius, low, high, refused, taken, dx, dy, word after, low after, high after}")
    for f in read(cases_path):
        dx, dy, word, low, high = answers[f[0]]
        taken = int(dx != "-")
        dx, dy = (dx, dy) if taken else (0, 0)
        print(f"{{ {f[1]}u, {f[2]}, {f[3]}u, {f[4]}u, {f[5]}, {taken}, {dx}, {dy}, {word}u, {low}u, {high}u }},")


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    if mode == "gen": gen(int(sys.argv[2]) if len(sys.argv) > 2 else 4000, int(sys.argv[3]) if len(sys.argv) > 3 else 20261128)
    elif mode == "game": game(sys.argv[2])
    elif mode == "inc": inc(sys.argv[2], sys.argv[3])
    else: sys.exit(__doc__)
