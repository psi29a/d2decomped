# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture game.exe 1.14d's cel anchor as C++ test cases.

Usage: uv run python cel_anchor.py > ../../tests/cel_anchor_cases.inc

Runs FUN_006014c0 (SRC/CelCmp.cpp, the clip-and-place step every DC6 and
DCC cel draw goes through) on a fake 800x600 8-bit surface. Only the row
writer it calls through 0x6e3688[0] (FUN_00608540) is cut off; its
arguments give the cel's on-screen rows. Each line is
{x, y, offset_x, offset_y, width, height, left, top, bottom}: the anchor,
the cel header, then the left column and the first and last screen rows
game.exe draws (top = bottom = -1 when it draws nothing).
"""
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EDX

import emu

SCREEN_W, SCREEN_H = 800, 600
ROW_WRITER = 0x608540


def main():
    path = Path(emu.data_path()) / 'bin/game.exe'
    if hashlib.sha256(path.read_bytes()).hexdigest() != '631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf':
        raise SystemExit('cel_anchor: requires stock game.exe 1.14d (1.14.3.71)')
    machine = emu.Emu(str(path))
    if machine.r32(0x6e3688) != ROW_WRITER:
        raise SystemExit('cel_anchor: unexpected row writer table')
    surface = 0x10000000          # never dereferenced: the row writer is hooked
    drawn = {}

    def row_writer(current):
        # EDX = surface + bottom row * pitch + left; stack: left, _, _, height, skipped, rows
        dest = current.mu.reg_read(UC_X86_REG_EDX)
        left, rows = current.arg(0), current.arg(5)
        bottom = (dest - surface - left) // SCREEN_W
        drawn.update(left=left, top=bottom - rows + 1, bottom=bottom)
        return 0

    machine.hook(ROW_WRITER, row_writer, 9)
    context = machine.alloc(0x40)
    cel = machine.alloc(0x20)
    machine.w32(context + 0x3c, cel)          # the cel; context+0x34 = 0 picks row writer 0
    for height in (1, 11, 32, 132):
        for offset_y in sorted({-height, -1, 0, 5, 132}):
            for offset_x in (-40, 0, 7):
                for x, y in ((100, 300), (400, 0), (400, 3), (400, 599), (400, 640), (400, 700), (400, -5)):
                    machine.w32(cel + 0x4, 24)
                    machine.w32(cel + 0x8, height)
                    machine.w32(cel + 0xc, offset_x)
                    machine.w32(cel + 0x10, offset_y)
                    drawn.clear()
                    machine.call(0x6014c0, context, x, y, 0, 0, surface, SCREEN_H, SCREEN_W, 0, 0, 0, limit=10000)
                    box = (drawn['left'], drawn['top'], drawn['bottom']) if drawn else (0, -1, -1)
                    print('{%d, %d, %d, %d, %d, %d, %d, %d, %d},' % (x, y, offset_x, offset_y, 24, height, *box))


if __name__ == '__main__':
    main()
