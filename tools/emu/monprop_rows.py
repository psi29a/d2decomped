# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's compiled MonProp rows (DataTables +0xc30, 0x138 bytes a row).

    uv run python monprop_rows.py

One line a row: its index, id, the MonStats rows pointing at it (MonStats
+0x1a), then per difficulty its {prop/par:min-max@chance} slots. The
layout as FUN_00573cb0 reads it: +0 id, then per difficulty (+4 + 0x60 d)
six {prop, par, min, max} ints, the chance bytes at +0x124 + 6 d + slot.
tests/game/test_game_tables.cpp's MonProp cases come from this.
"""
import struct

import drlg


def main():
    e = drlg.boot()
    tables = e.r32(0x744304)
    rows, count = e.r32(tables + 0xc30), e.r32(tables + 0xc34)
    monstats, monstats_count = e.r32(tables + 0xa78), e.r32(tables + 0xa80)
    users = {}
    for row in range(monstats_count):
        prop_row = struct.unpack("<h", e.read(monstats + row * 0x1a8 + 0x1a, 2))[0]
        if prop_row >= 0:
            users.setdefault(prop_row, []).append(row)
    for row in range(count):
        data = e.read(rows + row * 0x138, 0x138)
        per_difficulty = []
        for difficulty in range(3):
            slots = []
            for slot in range(6):
                prop, par, low, high = struct.unpack_from("<iiii", data, 4 + 0x60 * difficulty + 16 * slot)
                if prop < 0:
                    break
                slots.append(f"p{prop}/{par}:{low}-{high}@{data[0x124 + 6 * difficulty + slot]}")
            per_difficulty.append(" ".join(slots) or "-")
        print(row, struct.unpack_from("<i", data, 0)[0], "monstats", users.get(row, []), "|", " | ".join(per_difficulty))


if __name__ == "__main__":
    main()
