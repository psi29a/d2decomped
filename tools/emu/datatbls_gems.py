# SPDX-License-Identifier: GPL-3.0-or-later
"""Check ItemTbls.cpp FUN_00636b70's gem letter and Items reverse index.

Usage: D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_gems.py

CompileTxt and string lookup are hooked. The loader's three-byte letter
buffer, string-ID writes, and Items +0xf0 assignments run in game.exe on
synthetic records. No MPQ is read.
"""

import struct

from _monster_world import require
from monster_loaders import Rig


FUNCTION = 0x636B70
GEM_STRIDE = 0xC0
ITEM_STRIDE = 0x1A8
ITEMS = 0x96CA5C
GEMS = 0x96CA94
COUNT = 0x96CA90


def check(rows):
    """Verify lookup input bytes and the order of reverse item writes."""
    rig = Rig()
    rig.rows = len(rows)
    rig.table = rig.m.alloc(max(GEM_STRIDE, GEM_STRIDE * len(rows)))
    item_capacity = max(1, len(rows), max((index + 1 for index in rows if index >= 0), default=0))
    items = rig.m.alloc(ITEM_STRIDE * item_capacity)
    rig.m.w32(ITEMS, items)
    for index, item_index in enumerate(rows):
        row = rig.table + index * GEM_STRIDE
        rig.m.w32(row + 0x28, item_index)
    # The Items index and letter share the same four-byte field in game.exe:
    # the first three bytes supply the letter; the signed dword supplies the
    # reverse item index. Space in the low byte is item index 32.

    def lookup(current):
        """Record all four bytes of the stack letter buffer and return an ID."""
        pointer = current.arg(0)
        index = len([entry for entry in rig.log if entry[0] == "gem-letter"])
        rig.log.append(("gem-letter", rig.m.read(pointer, 4)))
        return 0x300 + index

    rig.m.hook(0x524D30, lookup, 2)
    result = rig.go(FUNCTION)
    require(result[0] == "ret", f"gems loader: {result}; {rig.log}")
    require((rig.m.r32(GEMS), rig.m.r32(COUNT)) == (rig.table, len(rows)),
            "gems table or count differs")
    seen = [entry[1] for entry in rig.log if entry[0] == "gem-letter"]
    expected_letters = [bytes(0 if ch == 0x20 else ch for ch in struct.pack("<I", index & 0xFFFFFFFF)[:3]) + b"\0"
                        for index in rows]
    require(seen == expected_letters, f"gem letter buffers {seen} != {expected_letters}")
    for index in range(len(rows)):
        require(rig.m.r16(rig.table + index * GEM_STRIDE + 0x2C) == 0x300 + index,
                f"gem row {index} missing string ID")
    expected_items = [0] * item_capacity
    for index, item_index in enumerate(rows):
        expected_items[index] = -1
        if 0 <= item_index < item_capacity:
            expected_items[item_index] = index
    observed_items = [rig.m.r32(items + index * ITEM_STRIDE + 0xF0) for index in range(item_capacity)]
    require(observed_items == [value & 0xFFFFFFFF for value in expected_items],
            f"gem reverse Items index {observed_items} != {expected_items}")


def main():
    """Exercise empty and multiple three-byte gem labels."""
    for rows in ((), (0,), (1, 0, -1, 32), (2, 2, 0, -1, 1)):
        check(rows)
    print("ok: FUN_00636b70 four gem-letter and reverse-index cases")


if __name__ == "__main__":
    main()
