# SPDX-License-Identifier: GPL-3.0-or-later
"""Check ItemTbls.cpp FUN_00638ae0's populated gamble index and level cuts.

Usage: D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_gamble.py

CompileTxt and item-code lookup are hooked. The original sort and level-cut
loops run on synthetic tables; no MPQ or host file is read.
"""

import random
import struct

from _monster_world import require
from monster_loaders import Rig


FUNCTION = 0x638AE0
ITEMS_ROWS = 0x96CA5C
ITEMS_LINK = 0x96BCC4
GAMBLE_COUNT = 0x96CAB0
GAMBLE_SORTED = 0x96CAB4
LEVEL_CUTS = 0x96CABC
FILE = ".\\DATATBLS\\ItemTbls.cpp"


def check(levels, seed, tie_order=None):
    """Compare sorted item indices, row rewrites, and all 99 level cuts."""
    rng = random.Random(seed)
    rig = Rig()
    count = len(levels)
    rig.rows = count
    rig.table = rig.m.alloc(max(0x100, count * 12))
    items = rig.m.alloc(max(0x1A8, count * 0x1A8))
    rig.m.w32(ITEMS_ROWS, items)
    rig.m.w32(ITEMS_LINK, 0x1234)
    row_bytes = bytearray(count * 12)
    for index in range(count):
        struct.pack_into("<III", row_bytes, 12 * index, index + 1, rng.randrange(1 << 30), 0)
        rig.write(items + 0x1A8 * index + 0xFD, bytes((levels[index],)))
    if count:
        rig.write(rig.table, row_bytes)

    def item_code(current):
        """Map each scripted row's distinct code to the corresponding item."""
        code = current.arg(1)
        rig.log.append(("item-code", current.arg(0), code, current.arg(2)))
        return code - 1 if 1 <= code <= count else -1

    rig.m.hook(0x6BD130, item_code, 3)
    result = rig.go(FUNCTION)
    require(result[0] == "ret", f"gamble loader did not return: {result}; {rig.log}")
    require(rig.m.r32(GAMBLE_COUNT) == count, "gamble count changed")
    sorted_ptr = rig.m.r32(GAMBLE_SORTED)
    sorted_indices = tuple(rig.m.r32(sorted_ptr + 4 * index) for index in range(count))
    expected_indices = (tie_order if tie_order is not None else
                        tuple(sorted(range(count), key=levels.__getitem__)))
    require(sorted_indices == expected_indices,
            f"gamble sorted indices {sorted_indices} != {expected_indices}")
    for index, item_index in enumerate(expected_indices):
        row = rig.table + 12 * index
        require((rig.m.r32(row), rig.m.r32(row + 4), rig.m.r32(row + 8)) ==
                (item_index + 1, levels[item_index], item_index),
                f"gamble sorted row {index} differs")
    cuts = tuple(rig.m.r32(LEVEL_CUTS + 4 * (level - 1)) for level in range(1, 100))
    expected_cuts = tuple(next((index for index, row in enumerate(expected_indices)
                                if levels[row] > level), count) for level in range(1, 100))
    require(cuts == expected_cuts, f"gamble level cuts {cuts} != {expected_cuts}")
    require(rig.m.r32(0x96CAB8) == 2, "gamble level sentinel changed")
    lookups = [entry for entry in rig.log if entry[0] == "item-code"]
    require([entry[2] for entry in lookups] == list(range(1, count + 1)),
            f"gamble item-code lookups {lookups}")
    frees = [entry for entry in rig.log if entry[0] == "unload"]
    require(frees == [("unload", rig.table)], f"gamble table release {frees}")
    allocations = [entry for entry in rig.log if entry[0] == "alloc"]
    require(allocations == [] if count == 0 else
            allocations == [("alloc", 0, 4 * count, FILE, 0x7DB, 0)],
            f"gamble index allocation {allocations}")


def main():
    """Exercise empty, singleton and mixed, distinctly ordered level sets."""
    cases = [(), (1,), (99,), (10, 3, 90, 21), (99, 1, 50, 10, 74, 2)]
    rng = random.Random(FUNCTION)
    cases.extend(tuple(rng.sample(range(1, 100), count)) for count in (2, 3, 6, 10, 20))
    for seed, levels in enumerate(cases):
        check(levels, seed)
    check((5, 5, 1, 5, 1, 99), 999, (2, 4, 3, 1, 0, 5))
    print(f"ok: FUN_00638ae0 {len(cases) + 1} populated gamble lists and 99 cuts each")


if __name__ == "__main__":
    main()
