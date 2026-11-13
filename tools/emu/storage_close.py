# SPDX-License-Identifier: GPL-3.0-or-later
"""Run game.exe's hit tests for the stash's and the Cube's close buttons (FUN_00489980, FUN_00489fb0) over a grid round each.

    uv run python storage_close.py [--exe Game.exe] [--dump ../../tests/storage_close_cases.inc]

800x600: the left panels' x 0x7a2858 = 80, y shift 0x7a285c = -60,
H 0x711470 = 600. FUN_0044dcc0 (the game's expansion flag) is hooked to
the case's; FUN_00486b00 (the bottom) runs for real. The stash's test
takes x in ESI, y in EDI; the Cube's x in EAX, y in EDX (no expansion
test: run once, as 0). The grid runs 3 px past each edge of the box
the draw puts the button at (FUN_0048edf0: x + 0x113 / 0x110).
A line: kind (1 the stash, 2 the Cube), expansion, x, y, 1 over it / 0 not.
"""
import argparse

import emu

# kind: (address, its expansions, its x and y registers, the box's left, top and size)
KINDS = {
    1: (0x489980, (0, 1), ("esi", "edi"), lambda expanded: (80 + (0x110 if expanded else 0x113), 540 - (0x40 if expanded else 0x41) - 0x23, 0x28)),
    2: (0x489FB0, (0,), ("eax", "edx"), lambda expanded: (80 + 0x113, 540 - 100, 0x28)),
}


def capture(exe):
    machine = emu.Emu(exe)
    expansion = [0]
    machine.hook(0x44DCC0, lambda e: expansion[0], 0)
    machine.w32(0x7A2858, 80)
    machine.w32(0x7A285C, -60)
    machine.w32(0x711470, 600)
    rows = []
    for kind, (address, expansions, (x_reg, y_reg), box) in KINDS.items():
        for expanded in expansions:
            expansion[0] = expanded
            left, top, size = box(expanded)
            for mouse_y in range(top - 3, top + size + 4):
                for mouse_x in range(left - 3, left + size + 4):
                    over = machine.call(address, regs={x_reg: mouse_x, y_reg: mouse_y}, limit=1000)
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
