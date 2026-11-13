# SPDX-License-Identifier: GPL-3.0-or-later
"""A player's mana a frame, with and without state 0x55, as the oracle: FUN_005806f0.

    uv run python mana_frame.py gen 50000 2 > cases.tsv
    uv run python mana_frame.py game cases.tsv > g.txt      # each case line + game.exe's amount
    ../../build/tests/test_mana_frame g.txt | diff g.txt -
    uv run python mana_frame.py gen 1000 1 > f.tsv           # the fixture test_mana_frame checks
    uv run python mana_frame.py game f.tsv > fg.txt
    uv run python mana_frame.py inc fg.txt > ../../tests/mana_frame_cases.inc

What the run shows (gen 50000 2: 100000 cases, half with the state on):
rules::mana_per_frame matches all of them, the 32-bit wraps and the
MulDiv's divide-first branches included.

FUN_005806f0 (EDI = the player unit) reads stat 8 (mana, FUN_00625480)
and its max (FUN_00625d60), then asks for state 0x55 (nomanaregen,
FUN_00639df0 at 0x58071d). Off, the ManaRegen part: the class's CharStats
row (unit +4, the table at 0x744304 +0xbc4, 0xc4 bytes a row) byte +0x3a
x 25 frames, or 7500 when that's 0, divides the max (32-bit idiv), at
least 1, then FUN_00483360(that, 100 + stat 0x1b, 100). On, that part is 0
(0x580724). Then stat 0x1a (manarecovery) is added (0x58079c). After that
come the caps (max - mana, -mana) and FUN_006272b0; this oracle stops
before them, at 0x58079e, and prints ESI: the amount a frame, uncapped.

A case line is `id state max regen bonus flat`: state 0 / 1, the max (from
FUN_00625d60), the ManaRegen byte, stat 0x1b, stat 0x1a. `game` hooks the
three getters and the state test, puts a one-row CharStats table at
0x744304, and runs FUN_005806f0 for real up to 0x58079e: its idiv, the
muldiv FUN_00483360 and the __alldiv it can reach, the 32-bit adds.
d2d's side is rules::mana_per_frame (tests/test_mana_frame.cpp).
"""
import random
import sys

from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_EDI, UC_X86_REG_ESI, UC_X86_REG_ESP

import emu

START, STOP = 0x5806F0, 0x58079E
TABLES = 0x744304
INT_MIN, INT_MAX = -(1 << 31), (1 << 31) - 1


def s32(value):
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value >> 31 else value


def gen(count, seed):
    rng = random.Random(seed)
    edges = (INT_MIN, INT_MIN + 1, -65537, -101, -100, -1, 0, 1, 99, 100, 0x10000, 0x10001, 0xFFFFF, 0x100000, 0x100001, INT_MAX - 1, INT_MAX)
    case = 0
    for _ in range(count):
        kind = rng.random()
        if kind < 0.5:                                       # play: what a character carries
            top = rng.choice((0, 256, rng.randrange(1, 5000) * 256 + rng.choice((0, rng.randrange(256)))))
            regen = rng.choice((0, 120, 200, 255, rng.randrange(256)))
            bonus = rng.choice((0, 0, rng.randrange(-100, 400)))
            flat = rng.choice((0, 0, rng.randrange(-50, 300)))
        elif kind < 0.8:                                     # the whole 32-bit range
            top, bonus, flat = (rng.randrange(INT_MIN, INT_MAX + 1) for _ in range(3))
            regen = rng.randrange(256)
        else:                                                # the branch edges of the idiv and FUN_00483360
            top = rng.choice(edges) if rng.random() < 0.5 else rng.choice(edges) * rng.choice((1, 25, 7500, 6375))
            top = s32(top)
            regen = rng.choice((0, 1, 255, rng.randrange(256)))
            bonus = s32(rng.choice(edges) + rng.choice((0, -100, -99)))
            flat = rng.choice(edges)
        for state in (0, 1):
            print(case, state, top, regen, bonus, flat, sep="\t")
            case += 1


class Game:
    def __init__(self):
        e = self.e = emu.Emu()
        self.unit = e.alloc(0x200)
        self.row = e.alloc(0xC4)
        tables = e.alloc(0x1000)
        e.w32(tables + 0xBC4, self.row)
        e.w32(tables + 0xBC8, 1)
        e.w32(TABLES, tables)
        e.w32(self.unit + 4, 0)                              # class 0: the table's one row
        self.now = {"state": 0, "max": 0, "bonus": 0, "flat": 0}
        stats = {8: 0, 0x1B: "bonus", 0x1A: "flat"}

        def stat(h):
            key = stats.get(h.arg(1))
            return 0 if not key else self.now[key] & 0xFFFFFFFF
        e.hook(0x625480, stat, 3)
        e.hook(0x625D60, lambda h: self.now["max"] & 0xFFFFFFFF, 1)
        e.hook(0x639DF0, lambda h: self.now["state"] if h.arg(1) == 0x55 else 0, 2)

    def run(self, state, top, regen, bonus, flat):
        e = self.e
        self.now.update(state=state, max=top, bonus=bonus, flat=flat)
        e.mu.mem_write(self.row + 0x3A, bytes((regen,)))
        sp = emu.STACK + emu.STACK_SIZE - 0x1000
        e.w32(sp, emu.RET_MAGIC)
        e.mu.reg_write(UC_X86_REG_ESP, sp)
        e.mu.reg_write(UC_X86_REG_EBP, 0)
        e.mu.reg_write(UC_X86_REG_EDI, self.unit)
        e.mu.emu_start(START, STOP)
        return s32(e.mu.reg_read(UC_X86_REG_ESI))


def game(path):
    g = Game()
    for line in open(path):
        case, state, top, regen, bonus, flat = map(int, line.split())
        print(case, state, top, regen, bonus, flat, g.run(state, top, regen, bonus, flat), sep="\t")


def inc(path):
    lines = [line.split() for line in open(path) if line.strip()]
    print(f"// Stock game.exe 1.14d, FUN_005806f0 up to 0x58079e; generated by tools/emu/mana_frame.py (gen {len(lines) // 2} 1, then game, then inc).")
    print("// state 0x55, max, ManaRegen byte, manarecoverybonus (0x1b), manarecovery (0x1a), the amount a frame (256ths)")
    for _case, state, top, regen, bonus, flat, amount in lines:
        print(f"    {{ {state}, {top}, {regen}, {bonus}, {flat}, {amount} }},")


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]))
    elif len(sys.argv) == 3 and sys.argv[1] == "game":
        game(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "inc":
        inc(sys.argv[2])
    else:
        sys.exit(__doc__)
