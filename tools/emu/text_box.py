# SPDX-License-Identifier: GPL-3.0-or-later
"""Original Latin text-box rows and draw coordinates for test_text_box --dump.

Feed identical tab-separated cases to this oracle and the C++ dump. Each
case is: font width height margin_x margin_y TAB text. Font TBLs and the
reference executable remain local. Font metrics, strings, wrapping and
placement run as original code; allocation, English locale, palette lookup
and the final drawing primitive are isolated. See frontend-text-box.md.
"""
import argparse
import sys
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-exe", type=Path)
    parser.add_argument("--font-dir", type=Path)
    parser.add_argument("--cases", type=Path)
    parser.add_argument("--make-cases", action="store_true")
    args = parser.parse_args()
    if args.make_cases:
        samples = ("", "Hello world", "Enter Host IP Address to Join Game",
                   "Are you sure that you want to delete this character?",
                   "Cannot detect a valid TCP/IP address.", "W" * 30,
                   " Hello  world ", "One two three four five six seven eight nine ten")
        for name in ("font16", "font24", "font42", "fontformal11"):
            for width in (80, 100, 200, 264, 272):
                for height, margin_x, margin_y in ((50, 0, 0), (120, 10, 8), (1, 0, 0)):
                    for text in samples:
                        print(f"{name} {width} {height} {margin_x} {margin_y}\t{text}")
        return 0
    if not (args.game_exe and args.font_dir and args.cases):
        parser.error("provide --game-exe, --font-dir and --cases, or --make-cases")
    machine = emu.Emu(str(args.game_exe))
    machine.hook(0x40b380, lambda current: current.alloc(current.mu.reg_read(UC_X86_REG_ECX)), 2)
    machine.hook(0x413020, lambda current: current.alloc(current.arg(0)), 0)
    machine.hook(0x412650, lambda current: 0, 0)
    machine.hook(0x687006, lambda current: int(current.arg(0) in (9, 10, 11, 12, 13, 32)), 0)
    machine.hook(0x525150, lambda current: 0, 0)  # English
    machine.hook(0x4fb180, lambda current: 0, 3)  # palette lookup, not layout
    machine.w32(0x841da0, 0x501650)  # original Latin glyph lookup
    machine.w32(0x6ce268, 1)
    machine.w32(0x6ce270, 1)
    fonts = {}
    for font_id, name in ((1, "font16"), (3, "font42"), (7, "font24"), (8, "fontformal11")):
        data = (args.font_dir / (name + ".tbl")).read_bytes()
        if len(data) < 3596 or data[:5] != b"Woo!\x01":
            raise ValueError("expected a 256-glyph Latin font: " + name)
        pointer = machine.put(data)
        slot = 0x841da8 + font_id * 20
        for offset, value in ((0, 1), (4, pointer), (8, pointer), (12, pointer + 12)):
            machine.w32(slot + offset, value)
        fonts[name] = (font_id, data)

    def wide_text(address):
        data = bytearray()
        while len(data) < 8192:
            pair = machine.read(address + len(data), 2)
            if pair == b"\0\0":
                return data.decode("utf-16-le").encode("latin-1").hex()
            data.extend(pair)
        raise ValueError("unterminated row")

    drawn = []

    def draw(current):
        drawn.append((current.mu.reg_read(UC_X86_REG_EDX), current.arg(0),
                      wide_text(current.mu.reg_read(UC_X86_REG_ECX))))
        return 0

    machine.hook(0x502320, draw, 3)
    for case_index, case in enumerate(args.cases.read_text(encoding="latin-1").splitlines()):
        fields, text = case.split("\t", 1)
        name, width, height, margin_x, margin_y = fields.split()
        width, height, margin_x, margin_y = map(int, (width, height, margin_x, margin_y))
        font_id, data = fonts[name]
        if width - 2 * margin_x <= max(data[15::14]):
            raise ValueError("original splitter may not progress in this narrow box")
        control = machine.alloc(0xb0)
        values = {0: 4, 8: 1, 12: 300, 16: 250, 20: width, 24: height,
                  0x40: margin_x, 0x44: margin_y, 0x5c: 2, 0x64: 1,
                  0x94: font_id, 0xa8: 4}
        for offset, value in values.items():
            machine.w32(control + offset, value)
        pointer = machine.put((text + "\0").encode("utf-16-le"))
        for address, text_pointer in ((0x4fd060, pointer), (0x4fbf30, 0)):
            result = machine.call(address, ecx=control, edx=text_pointer, limit=2000000)
            if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or result != 1:
                raise RuntimeError("original call failed or exceeded its instruction limit")
        for row_index, (x, baseline, text_hex) in enumerate(drawn):
            print(case_index, row_index, x, baseline, text_hex)
        drawn.clear()
    return 0


if __name__ == "__main__":
    sys.exit(main())
