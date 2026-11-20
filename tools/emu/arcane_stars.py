# SPDX-License-Identifier: GPL-3.0-or-later
"""The Arcane Sanctuary's stars, as the oracle: FUN_00476290 (and FUN_00476190).

    uv run python arcane_stars.py gen 2000 1 > cases.tsv
    uv run python arcane_stars.py game cases.tsv > g.txt   # each frame's draws, hashed
    uv run python arcane_stars.py inc 300 1 > ../../tests/arcane_stars_cases.inc

The world draw (FUN_00476bc0) calls FUN_00476290 in level 74. Its first
call seeds the star seed (*0x712c4c: FUN_00650e30, then FUN_00650e40 with
FUN_00650de0's clock mix), makes eight colours (FUN_004fb180, the palette at
0x81e668's nearest) and places the 256 stars (FUN_00476190 across the
screen, 0x7a5220 x 0x7a521c). Every call draws each star as a line from
(x, y) to itself in its colour (FUN_004f6380), then, more than 40 ms after
the last step (GetTickCount), moves each by its speed and puts one past the
left edge back in at the right.

A case line is `case seed width height palette t1 t2 ...`: the clock mix
FUN_00650de0 returns, the screen's size, a seed for a made-up palette (1024
bytes of rules::Rng(palette).next() & 0xff: byte 0 red, 1 green, 2 blue)
and the GetTickCount of each call. `game` hooks FUN_00650de0, GetTickCount
and the line draw, runs FUN_00476290 for real (FUN_00650e30 / e40,
FUN_00476190, FUN_004fb180 -> FUN_00605210 too) and prints, per call,
`case frame hash`: FNV-1a 64 over the draws' (x, y, colour) as 32-bit
little-endian words, in draw order. tests/test_arcane_stars.cpp prints the
same lines from rules::Stars.
"""
import random
import struct
import sys

import emu

MADE, STEPPED, WIDTH, HEIGHT = 0x7B955C, 0x7B57E0, 0x7A5220, 0x7A521C
SEED_PTR, PALETTE = 0x712C4C, 0x81E668
STARS, CLOCK_MIX, LINE = 0x476290, 0x650DE0, 0x4F6380
FNV_OFFSET, FNV_PRIME = 0xCBF29CE484222325, 0x100000001B3


def palette_bytes(seed):
    low, high = seed & 0xFFFFFFFF, 666
    out = bytearray()
    for _ in range(1024):
        product = low * 0x6AC690C5 + high
        low, high = product & 0xFFFFFFFF, product >> 32
        out.append(low & 0xFF)
    return bytes(out)


def fnv(words):
    value = FNV_OFFSET
    for word in words:
        for byte in struct.pack("<I", word & 0xFFFFFFFF):
            value = ((value ^ byte) * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return value


def cases(count, seed):
    rng = random.Random(seed)
    sizes = (0, 1, 2, 7, 8, 64, 480, 600, 640, 800, 1024, -1, -640)
    for case in range(count):
        width = rng.choice(sizes + (rng.randint(-50, 4096),))
        height = rng.choice(sizes + (rng.randint(-50, 4096),))
        clock = rng.choice((0, 1, 0xFFFFFFFF, rng.getrandbits(32)))
        mix = rng.getrandbits(32) if rng.random() < 0.5 else rng.getrandbits(31)
        ticks = []
        for _ in range(rng.randint(1, 60)):
            ticks.append(clock & 0xFFFFFFFF)
            clock += rng.choice((0, 1, 39, 40, 41, 42, rng.randint(0, 120), rng.randint(0, 2000)))
        yield case, mix, width, height, rng.getrandbits(32), ticks


def gen(count, seed):
    for case, mix, width, height, palette, ticks in cases(count, seed):
        print(case, mix, width, height, palette, *ticks)


def run(rows):
    e = emu.Emu()
    if e.r32(SEED_PTR) == 0:
        e.w32(SEED_PTR, e.alloc(8))
    state = {"now": 0, "mix": 0, "draws": []}
    for addr, (name, _) in list(e.slots.items()):
        if name == "GetTickCount":
            e.slots[addr] = (name, lambda e: (state["now"], 0))
    e.hook(CLOCK_MIX, lambda e: state["mix"], 0)

    def line(e):
        state["draws"] += [e.arg(0), e.arg(1), e.arg(4)]
        assert (e.arg(2), e.arg(3), e.arg(5)) == (e.arg(0), e.arg(1), 0xFF)
        return 0
    e.hook(LINE, line, 6)
    for case, mix, width, height, palette, ticks in rows:
        e.w32(MADE, 0)
        e.w32(STEPPED, 0)
        e.w32(WIDTH, width)
        e.w32(HEIGHT, height)
        e.mu.mem_write(PALETTE, palette_bytes(palette))
        state["mix"] = mix
        for frame, now in enumerate(ticks):
            state["now"], state["draws"] = now, []
            e.call(STARS)
            yield case, frame, fnv(state["draws"])


def read(path):
    for line in open(path):
        case, mix, width, height, palette, *ticks = map(int, line.split())
        yield case, mix, width, height, palette, ticks


def game(path):
    for case, frame, value in run(read(path)):
        print(case, frame, f"{value:016x}")


def inc(count, seed):
    rows = list(cases(count, seed))
    hashes = {}
    for case, _frame, value in run(rows):
        hashes.setdefault(case, []).append(value)
    print(f"// tools/emu/arcane_stars.py inc {count} {seed}: case, mix, width, height, palette, then each call's tick and draw hash")
    for case, mix, width, height, palette, ticks in rows:
        frames = ", ".join(f"{{ {now}u, 0x{value:016x}ull }}" for now, value in zip(ticks, hashes[case]))
        print(f"{{ {mix}u, {width}, {height}, {palette}u, {{ {frames} }} }},")


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]))
    elif len(sys.argv) == 3 and sys.argv[1] == "game":
        game(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "inc":
        inc(int(sys.argv[2]), int(sys.argv[3]))
    else:
        sys.exit(__doc__)
