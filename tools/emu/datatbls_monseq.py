# SPDX-License-Identifier: GPL-3.0-or-later
"""Check MonsterTbls.cpp FUN_00659c60's sequence index on game.exe 1.14d.

Usage: D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_monseq.py

CompileTxt and link creation are hooked. The index allocation and row walk
run in game.exe. Fixture sequence IDs are within the last row's ID because
the original sizes its index from that last row alone.
"""

import struct

from _monster_world import require
from monster_loaders import Rig


FUNCTION = 0x659C60
ROW_SIZE = 6
INDEX = 0x96C7B4
GROUP_COUNT = 0x96C7B8
FILE = ".\\DATATBLS\\MonsterTbls.cpp"


def check(sequences):
    """Compare group count, first-row pointer and both per-group counters."""
    rig = Rig()
    rig.rows = len(sequences)
    rig.table = rig.m.alloc(max(ROW_SIZE, ROW_SIZE * len(sequences)))
    rig.links = [rig.m.alloc(4)]
    for index, sequence in enumerate(sequences):
        rig.write(rig.table + ROW_SIZE * index, struct.pack("<h", sequence))
    result = rig.go(FUNCTION)
    require(result[0] == "ret", f"MonSeq loader: {result}; {rig.log}")
    count = sequences[-1] + 1 if sequences else 0
    index = rig.m.r32(INDEX)
    require(rig.m.r32(GROUP_COUNT) == count and bool(index) == bool(sequences),
            f"MonSeq group count/index for {sequences}")
    if sequences:
        for group in range(count):
            first = next((position for position, value in enumerate(sequences) if value == group), None)
            expected = ((rig.table + ROW_SIZE * first, sequences.count(group), sequences.count(group))
                        if first is not None else (0, 0, 0))
            actual = tuple(rig.m.r32(index + 12 * group + 4 * field) for field in range(3))
            require(actual == expected, f"MonSeq group {group}: {actual} != {expected}")
    allocations = [entry for entry in rig.log if entry[0] == "alloc"]
    expected_alloc = [("alloc", 0, count * 12, FILE, 0xCCC, 0)] if sequences else []
    require(allocations == expected_alloc, f"MonSeq allocation {allocations}")


def main():
    """Cover empty, duplicate, missing and out-of-order sequence IDs."""
    cases = ((), (0,), (0, 0, 1), (0, 2), (0, 1, 0, 2), (2, 0, 2))
    for sequences in cases:
        check(sequences)
    print(f"ok: FUN_00659c60 {len(cases)} MonSeq group-index cases")


if __name__ == "__main__":
    main()
