# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's kept hover label draw (UI\\ui.cpp FUN_00453060): where it places the text.

Usage: uv run python ui_label.py [--runs N]
       (export D2_MPQ_DIR=<dir with bin/game.exe> first)

FUN_00453060 draws the wide string at 0x7a2c80 (set by FUN_00452be0 /
FUN_00452d20) at an anchor (0x7a2d20, 0x7a2d24): W = glyph width + 8, x =
anchor x - W / 2 (arithmetic shift) but at least 0, and at most screen width
(0x71146c) - W when x + W passes it; y = anchor y + 2, at least font height
+ 7, at most screen height (0x711470) - 0x1f; it calls FUN_00502320(ECX =
text, EDX = x, y - 2, 0, 1) and then zeroes 0xa0 bytes at 0x7a2c80 (the anchor follows at 0x7a2d20, so it is left). An empty
string does nothing. Here FUN_00453060 runs for real with FUN_00501820 (the
width) and FUN_00501a40 (the line height) hooked to fixed answers and
FUN_00502320 hooked to log its arguments; the real FUN_00526800 (wide
length) and memset run. A Python model of the rule above is compared on random
anchors, widths, heights and screen sizes plus the clamp edges. No MPQs are
read.
"""
import argparse
import random

import checked_emu
import emu

TEXT, ANCHOR_X, ANCHOR_Y, SCREEN_W, SCREEN_H = 0x7A2C80, 0x7A2D20, 0x7A2D24, 0x71146C, 0x711470
DRAW, WIDTH, HEIGHT = 0x502320, 0x501820, 0x501A40


def model(px, py, width, height, sw, sh):
    """The placement rule, with signed 32-bit compares as the code does."""
    w = width + 8
    x = px - (w >> 1)
    if x < 0:
        x = 0
    if x + w > sw:
        x = sw - w
    h = height + 7
    y = py + 2
    if y - h < 0:
        y = h
    if y > sh - 0x1F:
        y = sh - 0x1F
    return x, y - 2


def main():
    """Run the sweep; assert every draw equals the model and the buffer is cleared once."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, default=2000)
    runs = parser.parse_args().runs
    e = checked_emu.CheckedEmu()
    state, drawn = {}, []
    e.hook(WIDTH, lambda e: state["width"], 0)
    e.hook(HEIGHT, lambda e: state["height"], 0)

    def draw(e):
        """FUN_00502320: log (ECX, EDX, y, colour, flag)."""
        regs = (e.mu.reg_read(emu.UC_X86_REG_ECX), e.mu.reg_read(emu.UC_X86_REG_EDX))
        drawn.append((*regs, e.s32(e.mu.reg_read(emu.UC_X86_REG_ESP) + 4), e.arg(1), e.arg(2)))
        return 0
    e.hook(DRAW, draw, 3)

    def s32(v):
        """A dword as signed."""
        return v - (1 << 32) if v & 0x80000000 else v
    rng = random.Random(0x453060)
    cases = [(0, 0, 0, 10, 640, 480), (0, 0, 120, 16, 640, 480), (639, 479, 120, 16, 640, 480), (320, 240, 33, 16, 800, 600)]
    cases += [(rng.randint(-50, 900), rng.randint(-50, 700), rng.randint(0, 400), rng.randint(0, 40), rng.choice((640, 800, 1024)), rng.choice((480, 600, 768)))
              for _ in range(runs)]
    for px, py, width, height, sw, sh in cases:
        e.mu.mem_write(TEXT, b"\xaa" * 0xB0)
        e.mu.mem_write(TEXT, "x\0".encode("utf-16-le"))
        for addr, value in ((ANCHOR_X, px), (ANCHOR_Y, py), (SCREEN_W, sw), (SCREEN_H, sh)):
            e.w32(addr, value)
        state["width"], state["height"] = width, height
        drawn.clear()
        e.call(0x453060)
        want = model(px, py, width, height, sw, sh)
        assert len(drawn) == 1, drawn
        ecx, edx, ypos, colour, flag = drawn[0]
        assert (ecx, s32(edx), s32(ypos), colour, flag) == (TEXT, want[0], want[1], 0, 1), (px, py, width, height, sw, sh, drawn, want)
        assert e.read(TEXT, 0xA0) == bytes(0xA0) and e.read(TEXT + 0xA8, 8) == b"\xaa" * 8
        assert e.r32(ANCHOR_X) == (px & 0xFFFFFFFF) and e.r32(ANCHOR_Y) == (py & 0xFFFFFFFF), "label clear changed anchor"
    e.mu.mem_write(TEXT, b"\0\0" + b"\xaa" * 0xAE)
    drawn.clear()
    e.call(0x453060)
    assert drawn == [] and e.read(TEXT + 2, 4) == b"\xaa" * 4, "an empty label draws or clears"
    print(f"ok: FUN_00453060 placed {len(cases)} labels as the clamp rule says, cleared the buffer, and ignored an empty one")


if __name__ == "__main__":
    main()
