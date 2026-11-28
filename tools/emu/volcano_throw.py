# SPDX-License-Identifier: GPL-3.0-or-later
"""A Volcano's throw, by game.exe 1.14d's own FUN_005afb80 (missile srvdofunc 28).

    uv run python volcano_throw.py gen [N] [SEED] > cases.txt    # synthetic cases (default 4000)
    uv run python volcano_throw.py game cases.txt > game.txt     # game.exe's answers (no game data)
    uv run python volcano_throw.py inc cases.txt game.txt > ../../tests/volcano_throw_cases.inc

tests/test_volcano_throw.cpp runs rules::volcano_throw on the same cases. A case:
`id data radius low high`: the missile's data +0x28 (rand(0x100) on its caster's
seed as it was made, FUN_005c8080; then each throw's), the radius (Missiles.txt
Param2) and the missile's seed before the throw (+0x20 / +0x24).

Game side: a synthetic table block at 0x744304 (one Missiles.txt row of 0x1a4: its
SubMissile1 +0x18 0, Param1 +0x38 1, Param2 +0x3c the radius, Param3 / Param4
+0x40 / +0x44 -1 / 1000; one Skills.txt row), FUN_005afb80 runs for real on a
type-3 unit with its data at +0x14, data +0x28 read and written by FUN_0064a730 /
FUN_0064a710. Hooked (not the claim): the skill and level FUN_0064a280 /
FUN_0064a210, the owner FUN_00552fd0, the age FUN_0064a3b0 (1), the subtile
FUN_0045adf0 / FUN_0045ae20 (5000, 6000), the missile maker FUN_0059fa30 (its
params' x / y at +0x1c / +0x20 recorded) and FUN_005ae1f0. Output `id | dx dy data
low high`: the offset thrown at, data +0x28 and the missile's seed after.
"""
import random
import sys

CELL_X, CELL_Y = 5000, 6000


def gen(n, seed):
    rng = random.Random(seed)
    for i in range(n):
        data = rng.choice((0, 1, 255, rng.randrange(256), rng.getrandbits(32)))
        radius = rng.choice((1, 2, 3, 4, 5, 7, 10, 20, rng.randrange(1, 40)))
        print(i, data, radius, rng.getrandbits(32), rng.choice((666, rng.getrandbits(32))))


def read(path):
    for line in open(path):
        f = line.split()
        if f: yield f


def game(path):
    import emu
    from unicorn.x86_const import UC_X86_REG_EDX
    e = emu.Emu()
    tables = e.alloc(0x1000)
    row, skills = e.alloc(0x1a4), e.alloc(0x23c)
    e.w32(0x744304, tables)
    e.w32(tables + 0xb64, row); e.w32(tables + 0xb6c, 1)
    e.w32(tables + 0xb98, skills); e.w32(tables + 0xba0, 1)
    e.mu.mem_write(row + 0x18, (0).to_bytes(2, "little"))
    e.w32(row + 0x38, 1); e.w32(row + 0x40, 0xFFFFFFFF); e.w32(row + 0x44, 1000)
    made = []

    def on_make(m):
        params = m.mu.reg_read(UC_X86_REG_EDX)
        made.append((m.s32(params + 0x1c), m.s32(params + 0x20)))
        return 0
    e.hook(0x64a280, lambda m: 0, 1)
    e.hook(0x64a210, lambda m: 1, 1)
    e.hook(0x552fd0, lambda m: 0x1000, 0)
    e.hook(0x64a3b0, lambda m: 1, 1)
    e.hook(0x45adf0, lambda m: CELL_X, 0)
    e.hook(0x45ae20, lambda m: CELL_Y, 0)
    e.hook(0x59fa30, on_make, 0)
    e.hook(0x5ae1f0, lambda m: 0, 0)
    unit, data = e.alloc(0x100), e.alloc(0x40)
    for f in read(path):
        i, word, radius, low, high = (int(v) for v in f)
        e.w32(row + 0x3c, radius)
        e.mu.mem_write(unit, b"\0" * 0x100); e.mu.mem_write(data, b"\0" * 0x40)
        e.w32(unit, 3); e.w32(unit + 0x14, data)
        e.w32(unit + 0x20, low); e.w32(unit + 0x24, high)
        e.w32(data + 0x28, word)
        made.clear()
        e.call(0x5afb80, 1, ecx=0x2000, edx=unit)
        dx, dy = (made[0][0] - CELL_X, made[0][1] - CELL_Y) if made else ("-", "-")
        print(i, "|", dx, dy, e.r32(data + 0x28), e.r32(unit + 0x20), e.r32(unit + 0x24))


def inc(cases_path, game_path):
    answers = {f[0]: f[2:] for f in read(game_path)}
    print("// tools/emu/volcano_throw.py inc: game.exe 1.14d's FUN_005afb80 on each case (see the script)")
    print("// {data, radius, low, high, dx, dy, data after, low after, high after}")
    for f in read(cases_path):
        dx, dy, word, low, high = answers[f[0]]
        print(f"{{ {f[1]}u, {f[2]}, {f[3]}u, {f[4]}u, {dx}, {dy}, {word}u, {low}u, {high}u }},")


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    if mode == "gen": gen(int(sys.argv[2]) if len(sys.argv) > 2 else 4000, int(sys.argv[3]) if len(sys.argv) > 3 else 20261128)
    elif mode == "game": game(sys.argv[2])
    elif mode == "inc": inc(sys.argv[2], sys.argv[3])
    else: sys.exit(__doc__)
