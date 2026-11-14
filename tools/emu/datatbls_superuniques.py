# SPDX-License-Identifier: GPL-3.0-or-later
"""Check MonsterTbls.cpp FUN_006552e0's hcIdx completeness and row cap.

Usage: D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_superuniques.py

CompileTxt and link creation are hooked; the 66-slot index and count guard
run in game.exe on synthetic rows. No MPQ is read.
"""

import struct

from _monster_world import require
from monster_loaders import Rig


FUNCTION = 0x6552E0
STRIDE = 0x34
COUNT = 0x96C70C
SLOTS = 0x96C710


def check(row_count):
    """Verify the 512-row clamp and the required 66 distinct hcIdx values."""
    rig = Rig()
    rig.rows = row_count
    rig.table = rig.m.alloc(max(STRIDE, row_count * STRIDE))
    rig.links = [rig.m.alloc(4)]
    for index in range(row_count):
        rig.m.w32(rig.table + STRIDE * index + 8, index if index < 66 else 66)
    result = rig.go(FUNCTION)
    expected_count = min(row_count, 512)
    require(rig.m.r32(COUNT) == expected_count,
            f"SuperUniques row cap {row_count} -> {rig.m.r32(COUNT)}")
    raw = rig.m.read(SLOTS, 132)
    require(len(raw) == 132, "SuperUniques hcIdx index too short")
    slots = struct.unpack("<66h", raw)
    expected = tuple(index if index < row_count else -1 for index in range(66))
    require(slots == expected, f"SuperUniques hcIdx slots {slots} != {expected}")
    require(result[0] == "ret" if row_count >= 66 else result == ("halt", 0x782),
            f"SuperUniques {row_count} rows: {result}")


def main():
    """Check the first complete set and both sides of the 512-row cap."""
    for count in (0, 65, 66, 512, 513, 600):
        check(count)
    print("ok: FUN_006552e0 six hcIdx completeness and row-cap cases")


if __name__ == "__main__":
    main()
