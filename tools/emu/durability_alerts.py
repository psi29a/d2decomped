# SPDX-License-Identifier: GPL-3.0-or-later
"""The worn items' durability alerts, as the oracle: FUN_00497fe0 and FUN_00497cf0.

    uv run python durability_alerts.py gen 2000 1 > cases.tsv
    uv run python durability_alerts.py game cases.tsv > g.txt   # each case line + game.exe's draws
    uv run python durability_alerts.py inc cases.tsv > ../../tests/durability_alerts_cases.inc

FUN_00497fe0 (the in-game draw, FUN_00456ee0 at 0x456f89) walks the body
slots at 0x7223e8 (1, 3, 8, 9, 10, 4, 5). Unless one holds an item with
durability (FUN_00629930), not stackable (FUN_006289f0), a max
(FUN_00625e00) and stat 0x48 * 100 / max at 20 or under, it draws nothing.
Otherwise each slot draws its icon (FUN_00497cf0) from Panel\\DuroIcon
(0x7bef60): an empty slot its outline, except the hands (4, 5), which draw
nothing empty.

A case line is `width automap hostile` and then the seven slots in the
walk's order, each `item has_durability stackable shield quiver durability
max`. `game` hooks the player (0x463dd0, +0x60 the inventory), the worn item
(0x63bde0), its leaves (0x629930, 0x6289f0, 0x625e00, 0x625480 stat 0x48,
0x629bb0 type 0x33, 0x62e740), the automap's UI flag (0x4538d0, ECX 10), the
roster's hostility (0x47a560) and the cel draw (0x4f6480). It runs both
functions for real (the memset 0x681ef0 too) with the screen width
0x71146c, and prints the case line, `|`, the draws' count and each draw's
frame, x and y.
"""
import random
import sys

from unicorn.x86_const import UC_X86_REG_ECX

import emu

PLAYER_INVENTORY = 0x60
SCREEN_WIDTH = 0x71146C
DURO_CEL = 0x7BEF60
CEL_MARK = 0x5EED0CE1
SLOTS = (1, 3, 8, 9, 10, 4, 5)       # 0x7223e8


def gen_slot(rng, healthy):
    if rng.random() < 0.25:
        return (0, 0, 0, 0, 0, 0, 0)
    most = rng.choice((0, 1, 2, 5, 10, 20, 24, 25, 40, 60, 99, 100, 250, 255,
                       rng.randint(1, 255), rng.randint(1, 255), rng.randint(256, 1 << 31), -rng.randint(2, 1000)))
    roll = rng.random()
    if healthy and most > 0:
        durability = rng.randint(most // 5 + 1, most) if rng.random() < 0.95 else most // 5   # 21 % up; 1 in 20 on the line
    elif roll < 0.6 and most > 0:
        durability = most * rng.randint(0, 30) // 100   # around the 20 % line
    elif roll < 0.85:
        durability = rng.randint(0, max(most, 0))
    else:
        durability = rng.choice((-1, -rng.randint(1, 1 << 31), rng.randint(0, (1 << 31) - 1)))
    return (1, int(rng.random() < 0.8), int(rng.random() < 0.1), int(rng.random() < 0.3),
            rng.choice((0, 0, 0, 0, 0, 5, 6, -3, 0x7FFF)), durability, most)


def gen(count, seed):
    rng = random.Random(seed)
    for _ in range(count):
        width = rng.choice((640, 800, 800, rng.randint(200, 4000)))
        healthy = rng.random() < 0.4
        slots = [gen_slot(rng, healthy) for _ in SLOTS]
        print(width, int(rng.random() < 0.3), int(rng.random() < 0.3), *[value for slot in slots for value in slot])


def parse(line):
    values = list(map(int, line.split()))
    width, automap, hostile = values[:3]
    slots = {slot: values[3 + 7 * index:10 + 7 * index] for index, slot in enumerate(SLOTS)}
    return width, automap, hostile, slots


def run_game(path):
    e = emu.Emu()
    player, inventory = e.alloc(0x100), 0x4E5E0000
    e.w32(player + PLAYER_INVENTORY, inventory)
    now = {}
    draws = []

    def item(e):
        return now["slots"][(e.arg(0) - 0x10000) >> 8]

    def worn(e):
        assert e.arg(0) == inventory
        slot = e.arg(1)
        return 0x10000 + (slot << 8) if slot in now["slots"] and now["slots"][slot][0] else 0

    def stat(e):
        assert e.arg(1) == 0x48 and e.arg(2) == 0
        return item(e)[5] & 0xFFFFFFFF

    def of_type(e):
        assert e.arg(1) == 0x33
        return item(e)[3]

    def ui_flag(e):
        assert e.mu.reg_read(UC_X86_REG_ECX) == 10
        return now["automap"]

    def draw(e):
        assert e.r32(e.arg(0) + 0x34) == CEL_MARK and (e.arg(3), e.arg(4), e.arg(5)) == (0xFFFFFFFF, 1, 0)
        draws.append((e.r32(e.arg(0)), e.arg(1), e.arg(2)))
        return 1

    e.hook(0x463DD0, lambda e: player, 0)
    e.hook(0x63BDE0, worn, 2)
    e.hook(0x629930, lambda e: item(e)[1], 1)
    e.hook(0x6289F0, lambda e: item(e)[2], 1)
    e.hook(0x625E00, lambda e: item(e)[6] & 0xFFFFFFFF, 1)
    e.hook(0x625480, stat, 3)
    e.hook(0x629BB0, of_type, 2)
    e.hook(0x62E740, lambda e: item(e)[4] & 0xFFFFFFFF, 1)
    e.hook(0x4538D0, ui_flag, 0)
    e.hook(0x47A560, lambda e: now["hostile"], 0)
    e.hook(0x4F6480, draw, 6)
    e.w32(DURO_CEL, CEL_MARK)
    for line in open(path):
        width, automap, hostile, slots = parse(line)
        now.update(slots=slots, automap=automap, hostile=hostile)
        e.w32(SCREEN_WIDTH, width)
        draws.clear()
        e.call(0x497FE0)
        signed = [(frame, x - (1 << 32) if x >> 31 else x, y) for frame, x, y in draws]
        yield line.split(), signed


def game(path):
    for case, draws in run_game(path):
        print(*case, "|", len(draws), *[value for draw in draws for value in draw])


def inc(path):
    print("// Stock game.exe 1.14d, FUN_00497fe0 / FUN_00497cf0; generated by tools/emu/durability_alerts.py (gen 2000 1, then inc).")
    print("// width, automap, hostile, {7 slots: item, has_durability, stackable, shield, quiver, durability, max}, draws, {frame, x, y}...")
    for case, draws in run_game(path):
        values = list(map(int, case))
        slots = ", ".join("{ " + ", ".join(map(str, values[3 + 7 * index:10 + 7 * index])) + " }" for index in range(7))
        icons = ", ".join("{ %d, %d, %d }" % draw for draw in draws) or "{ 0, 0, 0 }"
        print("    { %d, %d, %d, { { %s } }, %d, { { %s } } }," % (values[0], values[1], values[2], slots, len(draws), icons))


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]))
    elif len(sys.argv) == 3 and sys.argv[1] in ("game", "inc"):
        (game if sys.argv[1] == "game" else inc)(sys.argv[2])
    else:
        sys.exit(__doc__)
