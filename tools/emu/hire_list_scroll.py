# SPDX-License-Identifier: GPL-3.0-or-later
"""The hire window's list box scroll bar: the thumb and what moves the list.

    uv run python hire_list_scroll.py gen 20000 1 > cases.tsv
    uv run python hire_list_scroll.py [--exe PATH] game cases.tsv > g.txt   # each case line + game.exe's answers
    ../../build/tests/test_hire_list_scroll cases.tsv | diff g.txt -
    uv run python hire_list_scroll.py inc g.txt > ../../tests/hire_list_scroll_cases.inc   # (gen 2000 1)

D2Win's ListBoxImplementation (FUN_004bf650), its fields as
hire_list_box.py sets them, plus its vtable (0x6daaec), the selected row
+0x180738 and the thumb's length +0x180718 (0x12). Each case first runs
FUN_004be110 (fastcall: the rows wholly shown into +0x18074e, the thumb's
cells into +0x180752 / +0x180756 and its cels through FUN_004f6480, hooked
and recorded: {frame, x, y}), as the draw before an input does; then one
input, for real: 1 a release on the track (FUN_004bf2f0, the event's u16
y +0xe), 2 an up step (FUN_004be910), 3 a down step (FUN_004be990), 4 a
wheel notch (FUN_004be860, the event's s16 +0xa: 1 up, -1 down; through
the vtable's +0x1c / +0x20, real; FUN_00420290 hooked), 0 none.

A case: `seq y height gap scroll_x first selected op arg rows {lines...}`.
`game` prints the case line, then `= whole top bottom first selected` and
each thumb cel as `frame:x:y`.
"""
import random
import struct
import sys

import emu

ROW, LINE = 0x300E, 0x800
Y, HEIGHT, LINE_H, GAP, SCROLL_X, SCROLL_Y, CELLS = 0x18070C, 0x180714, 0x180766, 0x18076A, 0x18075A, 0x18075E, 0x180762
CALLBACK, COUNT, FIRST, SELECTED, LENGTH, WHOLE, TOP, BOTTOM = 0x18074A, 0x180748, 0x180730, 0x180738, 0x180718, 0x18074E, 0x180752, 0x180756
SIZE, VTABLE = 0x180800, 0x6DAAEC


def gen(count, seed):
    rng = random.Random(seed)
    for seq in range(count):
        hire = rng.random() < 0.6                                  # the hire window's box: two lines a row
        rows = rng.choice((0, 1, 6, 7, 8, 9, 10, rng.randint(0, 20), rng.randint(0, 128)))
        lines = [2 if hire else rng.choice((0, 1, 2, 2, 3, 6, rng.randint(0, 6))) for _ in range(rows)]
        y = 140 if hire else rng.randint(0, 400)
        height = 0x118 if hire else rng.choice((0x10, 0x20, 0x30, 0x118, rng.randint(0x10, 500)))
        gap = 6 if hire else rng.choice((0, 6, rng.randint(0, 30)))
        scroll_x = 0x273 if hire else rng.randint(20, 780)
        first = rng.randint(0, rows) if rows and rng.random() < 0.8 else 0
        selected = rng.choice((-1, 0, first, rng.randint(-1, max(rows, 0)), rows - 1 if rows else 0))
        op = rng.choice((0, 1, 1, 2, 3, 4))
        if op == 1:
            cells = 1 if height < 0x20 else (height - 0x10) // 0xC - 1
            arg = rng.randint(y + 0x10 - 4, y + 0x10 + 12 * cells + 4)
        elif op == 4:
            arg = rng.choice((1, -1))
        else:
            arg = 0
        print(seq, y, height, gap, scroll_x, first, selected, op, arg, rows, *lines)


def game(path, exe=None):
    e = emu.Emu(exe=exe)
    box = e.alloc(SIZE)
    event = e.alloc(0x40)
    cels = []

    def cel(e):                                                   # FUN_004f6480(cel, x, y, -1, 5, 0)
        cels.append((e.r32(e.arg(0)), e.s32(e.mu.reg_read(emu.UC_X86_REG_ESP) + 8), e.s32(e.mu.reg_read(emu.UC_X86_REG_ESP) + 12)))
        return 0

    e.hook(0x4F6480, cel, 6)
    e.hook(0x420290, lambda e: 0, 1)
    e.w32(box, VTABLE)
    e.w32(box + LINE_H, 0xE)
    e.w32(box + CALLBACK, 0x4B3660)
    e.w32(box + LENGTH, 0x12)
    last_rows = 0
    signed = lambda value: value - (1 << 32) if value >> 31 else value
    for line in open(path):
        field = list(map(int, line.split()))
        seq, y, height, gap, scroll_x, first, selected, op, arg, rows = field[:10]
        lines = field[10:10 + rows]
        for row in range(max(rows, last_rows)):
            have = lines[row] if row < rows else 0
            for k in range(6):
                e.mu.mem_write(box + row * ROW + 0x8 + k * LINE, struct.pack("<H", 0x41 if k < have else 0))
        last_rows = rows
        cells = 1 if height < 0x20 else (height - 0x10) // 0xC - 1
        for offset, value in ((Y, y), (HEIGHT, height), (GAP, gap), (SCROLL_X, scroll_x), (SCROLL_Y, y + 0x10), (CELLS, cells),
                              (FIRST, first), (SELECTED, selected), (WHOLE, 1), (TOP, 0), (BOTTOM, 0)):
            e.w32(box + offset, value)
        e.mu.mem_write(box + COUNT, struct.pack("<H", rows))
        cels.clear()
        e.call(0x4BE110, ecx=box)
        drawn = list(cels)
        whole, top, bottom = signed(e.r32(box + WHOLE)), signed(e.r32(box + TOP)), signed(e.r32(box + BOTTOM))
        e.mu.mem_write(event, bytes(0x40))
        if op == 1:
            e.mu.mem_write(event + 0xE, struct.pack("<H", arg & 0xFFFF))
            e.call(0x4BF2F0, event, ecx=box)
        elif op == 2:
            e.call(0x4BE910, ecx=box)
        elif op == 3:
            e.call(0x4BE990, ecx=box)
        elif op == 4:
            e.mu.mem_write(event + 0xA, struct.pack("<h", arg))
            e.call(0x4BE860, event, ecx=box)
        print(seq, y, height, gap, scroll_x, first, selected, op, arg, rows, *lines, "=", whole, top, bottom,
              signed(e.r32(box + FIRST)), signed(e.r32(box + SELECTED)), *(f"{f}:{x}:{yy}" for f, x, yy in drawn))


def inc(path):
    print("// Stock game.exe 1.14d, FUN_004be110 / FUN_004bf2f0 / FUN_004be910 / FUN_004be990 / FUN_004be860;")
    print("// generated by tools/emu/hire_list_scroll.py (gen 2000 1, game, inc).")
    print('// { "the case line", "whole top bottom first selected frame:x:y..." }')
    for line in open(path):
        case, answers = line.split("=")
        print(f'    {{ "{case.strip()}", "{answers.strip()}" }},')


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
