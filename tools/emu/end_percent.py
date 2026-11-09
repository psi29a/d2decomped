# SPDX-License-Identifier: GPL-3.0-or-later
"""A potion's end percent, as S->C 0x18 carries it: FUN_005485b0 / FUN_00548640.

    uv run python end_percent.py gen 20000 1 > cases.tsv
    uv run python end_percent.py [--exe PATH] game cases.tsv > g.txt   # each case line + game.exe's percent
    ../../build/tests/test_hud_globe cases.tsv | diff g.txt -
    uv run python end_percent.py inc g.txt > ../../tests/end_percent_cases.inc       # the test's fixture (gen 2000 1)

FUN_005485b0 (EAX = the unit) is the life globe's preview: with state 100
(healthpot) on, ((the state's hpregen x (its end - the game's frame) + life)
>> 8) x 100 / (max life >> 8). FUN_00548640 the mana globe's: with state 0x6a
(manapot), ((end - frame) x (stat 0x1a + (stat 0x1b + 100) x steady / 100)
+ mana) x 100 / max mana, steady = max(max mana / (CharStats +0x3a x 25, or
7500), 1). Both in 32-bit signed arithmetic (imul, sar, idiv), then the low
byte, 100 when that's past 100 (cmp al, 0x64; ja). FUN_00548760 sends them
in 0x18; d2d's rules::life_end_percent / mana_end_percent (with
rules::mana_per_frame) are the port.

A case line is `seq kind cur max rate bonus regen end now`: kind 0 life
(rate the state's hpregen, stat 0x4a in 256ths a frame), kind 1 mana (rate
stat 0x1a, bonus stat 0x1b, regen the class's CharStats +0x3a). cur and max
in 256ths, end the state's end frame (FUN_00626090), now the game's frame
(+0xa8). The state is always on: no state is 0 before any arithmetic.
`game` hooks the stat getters (FUN_00625480, FUN_00625d00), the max
(FUN_00625d10 / FUN_00625d60), the state (FUN_006256b0), its end
(FUN_00626090) and the game (FUN_00554010), points the data tables
(0x744304) at one CharStats row, runs the function and prints the case line
with its low byte after it.
"""
import random
import sys

import emu

DATA_TABLES = 0x744304
CHARSTATS_ROW, CHARSTATS_ROWS, CHARSTATS_COUNT = 0xC4, 0xBC4, 0xBC8
HEALTHPOT, MANAPOT = 100, 0x6A
INT_MIN, INT_MAX = -(1 << 31), (1 << 31) - 1


def signed(value):
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value >> 31 else value


def gen(count, seed):
    rng = random.Random(seed)

    def fixed(points):
        return (points << 8) + rng.choice((0, 0, 0x80, 0xFF, rng.randrange(256)))

    for seq in range(count):
        kind = seq & 1
        shape = rng.random()
        if shape < 0.7:                                           # a character's own: points to 5000, a potion's rate
            top = fixed(rng.randint(1, 5000))
            cur = rng.choice((0, top, rng.randrange(top + 1), rng.randrange(top + 1), top + rng.randrange(1, 0x800)))
            rate = rng.choice((0, rng.randint(1, 0x40), rng.randint(1, 0x400), rng.randint(1, 0x2000)))
            frames = rng.choice((0, 1, rng.randint(1, 50), rng.randint(1, 500)))
        elif shape < 0.85:                                        # edges: under a point of max, past 100 % and 255 %
            top = rng.choice((0, 1, 0xFF, 0x100, 0x1FF, 0x200, fixed(rng.randint(1, 20))))
            cur = rng.choice((0, top, top * 2, top * 3 - 1, top * 3, rng.randrange(0x10000)))
            rate = rng.randint(0, 0x1000)
            frames = rng.choice((0, rng.randint(-10, 10), rng.randint(1, 400)))
        else:                                                     # the whole 32-bit range: where imul wraps
            top = rng.choice((rng.randint(INT_MIN, INT_MAX), rng.randint(1, 1 << 30), rng.randint(-0x10000, 0x10000)))
            cur = rng.randint(INT_MIN, INT_MAX)
            rate = rng.choice((rng.randint(INT_MIN, INT_MAX), rng.randint(0, 0x10000)))
            frames = rng.choice((rng.randint(-0x10000, 0x10000), rng.randint(INT_MIN, INT_MAX)))
        bonus = rng.choice((0, 0, rng.randint(-100, 500), rng.randint(INT_MIN // 4, INT_MAX // 4))) if kind else 0
        regen = rng.choice((0, 50, 75, 100, 120, 160, rng.randrange(256))) if kind else 0
        now = rng.choice((0, rng.randrange(1 << 32), (1 << 32) - rng.randint(1, 600)))
        end = (now + frames) & 0xFFFFFFFF
        print(seq, kind, cur, top, rate, bonus, regen, signed(end), signed(now))


def game(path, exe=None):
    e = emu.Emu(exe=exe)
    unit, state, game_struct = e.alloc(0x100), e.alloc(0x40), e.alloc(0x100)
    tables, row = e.alloc(0xC00), e.alloc(CHARSTATS_ROW)
    e.w32(unit + 4, 0)                                            # class 0, the one CharStats row
    e.w32(tables + CHARSTATS_ROWS, row)
    e.w32(tables + CHARSTATS_COUNT, 1)
    e.w32(DATA_TABLES, tables)
    now = {}
    stats = {}
    e.hook(0x625480, lambda e: stats[e.arg(1)] if e.arg(0) == unit else 0, 3)   # FUN_00625480(unit, stat, layer)
    e.hook(0x625D00, lambda e: now["rate"] if (e.arg(0), e.arg(1)) == (state, 0x4A) else 0, 3)
    e.hook(0x625D10, lambda e: now["max"], 1)
    e.hook(0x625D60, lambda e: now["max"], 1)
    e.hook(0x6256B0, lambda e: state if e.arg(1) == now["state"] else 0, 2)
    e.hook(0x626090, lambda e: now["end"] if e.arg(0) == state else 0, 1)
    e.hook(0x554010, lambda e: game_struct, 0)                    # fastcall, ECX the unit
    for line in open(path):
        seq, kind, cur, top, rate, bonus, regen, end, frame = map(int, line.split())
        e.w32(game_struct + 0xA8, frame)
        e.mu.mem_write(row + 0x3A, bytes((regen,)))
        now.update(max=top, rate=rate, end=end, state=MANAPOT if kind else HEALTHPOT)
        stats.clear()
        stats.update({6: cur, 8: cur, 0x1A: rate, 0x1B: bonus})
        percent = e.call(0x548640 if kind else 0x5485B0, regs={"eax": unit}) & 0xFF
        print(seq, kind, cur, top, rate, bonus, regen, end, frame, percent)


def inc(path):
    print("// Stock game.exe 1.14d, FUN_005485b0 / FUN_00548640; generated by tools/emu/end_percent.py (gen 2000 1, game, inc).")
    print("// kind (0 life, 1 mana), cur, max, rate, bonus, regen, end, now, percent")
    for line in open(path):
        _, *fields = line.split()
        print("    { " + ", ".join(fields) + " },")


def main():
    args = sys.argv[1:]
    exe = None
    if "--exe" in args:
        at = args.index("--exe")
        exe = args[at + 1]
        del args[at:at + 2]
    if len(args) == 3 and args[0] == "gen":
        gen(int(args[1]), int(args[2]))
    elif len(args) == 2 and args[0] == "game":
        game(args[1], exe)
    elif len(args) == 2 and args[0] == "inc":
        inc(args[1])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
