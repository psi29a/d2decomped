# SPDX-License-Identifier: GPL-3.0-or-later
"""Check ItemTypes and MonType matrix loaders across the 32-bit boundary.

Usage: D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_type_matrices.py

CompileTxt and link creation are hooked. The original 33x33 is-a walks and
matrix writes run on synthetic rows, including a row-1 -> row-32 relation.
"""

import struct

import _monster_world as mw
from _monster_world import require
from monster_loaders import Rig


SPECS = ((0x638D80, 0xE4, 4, 0x96C830, 0x96C834, True),
         (0x658520, 0x0C, 2, 0x96C878, 0x96C874, False))
ROW_COUNT = 33


def check(spec):
    """Verify every matrix word for zero rows plus one cross-word relation."""
    function, row_size, equiv_offset, stride_slot, matrix_slot, item_types = spec
    rig = Rig()
    mw.INSTRUCTION_LIMIT = 2_000_000
    rig.rows = ROW_COUNT
    rig.table = rig.m.alloc(ROW_COUNT * row_size)
    rig.links = [rig.m.alloc(4)]
    rig.write(rig.table + row_size + equiv_offset, struct.pack("<h", 32))
    if item_types:
        rig.logger(0x6BD230, 2, "link-fill", regs=())
    result = rig.go(function)
    require(result[0] == "ret", f"type matrix {function:#x}: {result}; {rig.log[-6:]}")
    stride = rig.m.r32(stride_slot)
    matrix = rig.m.r32(matrix_slot)
    require(stride == 2 and matrix != 0, f"type matrix {function:#x} stride {stride}")
    expected = []
    for source in range(ROW_COUNT):
        first = 1 if item_types else 0
        second = 0
        if source:
            if source < 32:
                first |= 1 << source
            else:
                second |= 1
        if source == 1:
            second |= 1
        expected.extend((first, second))
    actual = [rig.m.r32(matrix + 4 * index) for index in range(2 * ROW_COUNT)]
    require(actual == expected, f"type matrix {function:#x} differs: {actual} != {expected}")
    fills = [entry for entry in rig.log if entry[0] == "link-fill"]
    require(len(fills) == (ROW_COUNT if item_types else 0),
            f"type matrix {function:#x} link fills {len(fills)}")


def main():
    """Check both loader implementations on the same 33-row shape."""
    for spec in SPECS:
        check(spec)
    print("ok: 33x33 ItemTypes and MonType matrices cross one dword boundary")


if __name__ == "__main__":
    main()
