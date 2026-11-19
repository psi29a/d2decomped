# SPDX-License-Identifier: GPL-3.0-or-later
"""The hover bar's immunity rows, as the oracle: the table FUN_004526c0 walks.

    uv run python immunity_table.py > ../../tests/immunity_table_cases.inc

FUN_004526c0 (the last call of the hover text FUN_00454ad0) walks the
rows from 0x6d6418 to 0x6d6454, 10 bytes each: a dword stat, a word
string id, a dword font colour; a stat over 99 adds that string as a line
in that colour (unless the monster's class is 0x2c0..0x2c5). This reads
the six rows from game.exe and prints one `{stat, string, colour}` line
each, the file d2d's test compares with rules::kImmunities.
"""
import struct

import emu

TABLE, ROWS, ROW = 0x6d6418, 6, 10


def main():
    e = emu.Emu()
    print("// tools/emu/immunity_table.py: game.exe's rows at 0x6d6418 {stat, string, colour}")
    for row in range(ROWS):
        stat, string, colour = struct.unpack("<IHI", e.read(TABLE + row * ROW, ROW))
        print(f"{{{stat}, 0x{string:x}, {colour}}},")


if __name__ == "__main__":
    main()
