# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's automap top-right text lines: "EXPANSION", "v 1.14d" and
"Difficulty: %s" (FUN_00459ff0, FUN_0045a0a0, FUN_0045a190; docs/research/
re/automap.md "The top right").

Usage: uv run python automap_lines.py   (export D2_MPQ_DIR=<dir with bin/game.exe> first)

Each runs game.exe's own code with the string table lookup (FUN_00524a30)
answering a marker "[<id>]", the difficulty (FUN_0044dcd0) as the case says
and the measure (FUN_00501820) and draw (FUN_00502320) calls recorded:
their line y (DAT_007a51bc), colour and text. The measure keeps the three
pushed arguments (y, 4, 0) on the stack for the draw to pop. Checked: the
string ids, the colour 4, the line step of 0x10, the text's x = the screen's
width DAT_007a5220 less its measured width less 0x10, and that Normal (0)
draws no difficulty line. The wide-string copy and format
(FUN_005267e0, FUN_005269d0, FUN_00526320) and the C format (FUN_00413a40)
are game.exe's own.
"""
import sys

import checked_emu
import emu

Y, RIGHT = 0x7a51bc, 0x7a5220


def wide(e, address):
    """The UTF-16 string at `address`."""
    raw = bytearray()
    while e.read(address + len(raw), 2) != b"\0\0":
        raw += e.read(address + len(raw), 2)
    return raw.decode("utf-16-le")


def main():
    """Run the three line functions and check what they draw."""
    e = checked_emu.CheckedEmu()
    e.crt_init()
    state = {"difficulty": 0, "drawn": []}
    markers = {}

    def string(machine):
        """FUN_00524a30: the wide marker "[<id>]" for string id ECX."""
        ident = machine.mu.reg_read(emu.UC_X86_REG_ECX)
        if ident not in markers:
            text = "D:%s" if ident == 0x1057 else f"[{ident:#x}]"   # 0x1057 is the format "Difficulty: %s"
            markers[ident] = machine.put(text.encode("utf-16-le") + b"\0\0")
        return markers[ident]

    def draw(machine):
        """FUN_00502320 (text ECX, right edge EDX, y, colour, 0 on the stack): record it."""
        state["drawn"].append((wide(machine, machine.mu.reg_read(emu.UC_X86_REG_ECX)), machine.mu.reg_read(emu.UC_X86_REG_EDX),
                               machine.arg(0), machine.arg(1), machine.arg(2)))
        return 0

    e.hook(0x524a30, string, 0)
    e.hook(0x44dcd0, lambda machine: state["difficulty"], 0)
    e.hook(0x501820, lambda machine: 8 * len(wide(machine, machine.mu.reg_read(emu.UC_X86_REG_ECX))), 0)   # width: 8 px a character
    e.hook(0x502320, draw, 3)
    e.w32(RIGHT, 800)
    def line_of(text, start):
        """What one drawn line should be: x from the right edge, y, colour 4, 0."""
        return (text, 800 - 0x10 - 8 * len(text), start, 4, 0)

    checked = 0
    for start in (0x18, 0x28, 0x68):
        for name, address in (("expansion", 0x459ff0), ("version", 0x45a0a0)):
            e.w32(Y, start)
            state["drawn"].clear()
            e.call(address)
            text = {"expansion": "[0x58ca]", "version": "v 1.14d"}[name]
            assert state["drawn"] == [line_of(text, start)], (name, state["drawn"])
            assert e.r32(Y) == start + 0x10, name
            checked += 1
        for difficulty in (0, 1, 2):
            state["difficulty"] = difficulty
            e.w32(Y, start)
            state["drawn"].clear()
            e.call(0x45a190)
            if difficulty == 0:
                assert state["drawn"] == [] and e.r32(Y) == start
            else:
                # string 0x1057 ("Difficulty: %s") of 0x1423 Nightmare / 0x1422 Hell
                assert len(state["drawn"]) == 1, state["drawn"]
                assert state["drawn"][0] == line_of(state["drawn"][0][0], start), state["drawn"]
                assert state["drawn"][0][0] == f"D:[{0x1423 if difficulty == 1 else 0x1422:#x}]", state["drawn"]
                assert e.r32(Y) == start + 0x10
            checked += 1
    print(f"ok: {checked} automap top-right line runs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
