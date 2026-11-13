# SPDX-License-Identifier: GPL-3.0-or-later
"""Run game.exe's hit test for the stash's close button (FUN_00489980) over a grid round it.

    uv run python storage_close.py [--exe Game.exe] [--dump ../../tests/storage_close_cases.inc]

800x600: the left panels' x 0x7a2858 = 80, y shift 0x7a285c = -60,
H 0x711470 = 600. FUN_0044dcc0 (the game's expansion flag) is hooked to
the case's; FUN_00486b00 (the bottom) runs for real. The stash's test
takes x in ESI, y in EDI. The grid runs 3 px past each edge of
the box the draw puts the button at (FUN_0048edf0: x + 0x113 / 0x110).
A line: kind (1 the stash), expansion, x, y, 1 over it / 0 not.
"""
import argparse

import emu

KINDS = {1: 0x489980}


def capture(exe):
    machine = emu.Emu(exe)
    expansion = [0]
    machine.hook(0x44DCC0, lambda e: expansion[0], 0)
    machine.w32(0x7A2858, 80)
    machine.w32(0x7A285C, -60)
    machine.w32(0x711470, 600)
    rows = []
    for kind, address in KINDS.items():
        for expanded in (0, 1):
            expansion[0] = expanded
            left = 80 + (0x110 if expanded else 0x113)
            bottom = 540 - (0x40 if expanded else 0x41)
            for mouse_y in range(bottom - 0x23 - 3, bottom + 5 + 4):
                for mouse_x in range(left - 3, left + 0x28 + 4):
                    over = machine.call(address, regs={"esi": mouse_x, "edi": mouse_y}, limit=1000)
                    rows.append((kind, expanded, mouse_x, mouse_y, over))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe")
    parser.add_argument("--dump")
    args = parser.parse_args()
    rows = capture(args.exe)
    if args.dump:
        with open(args.dump, "w", newline="\n") as out:
            out.write("// tools/emu/storage_close.py: kind, expansion, x, y, over game.exe's close button\n")
            for row in rows:
                out.write("{" + ", ".join(str(value) for value in row) + "},\n")
    hits = sum(row[4] for row in rows)
    print(f"{len(rows)} points, {hits} over")


if __name__ == "__main__":
    main()
