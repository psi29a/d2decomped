# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_0061de00's LevelDefs-to-portal-array call on game.exe 1.14d.

Usage: D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_leveldefs.py

CompileTxt is hooked. FUN_0061de00 and its FUN_0061dd00 portal-array
builder run on synthetic LevelDefs rows; no MPQ is read.
"""

from _monster_world import require
from monster_loaders import Rig


FUNCTION = 0x61DE00
ROW_SIZE = 0x9C
LEVELS = 0x96C890
PORTAL_COUNT = 0x96C9F8
PORTAL_ARRAY = 0x96C9F4
ITEMS_POINTER = 0x744304
LEVEL_COUNT_OFFSET = 0xC5C
FILE = ".\\DATATBLS\\LvlTbls.cpp"


def check(flags):
    """Verify the integrated portal count, allocation and row-order index."""
    rig = Rig()
    rig.rows = len(flags)
    rig.table = rig.m.alloc(max(ROW_SIZE, ROW_SIZE * len(flags)))
    items = rig.m.alloc(LEVEL_COUNT_OFFSET + 4)
    rig.m.w32(ITEMS_POINTER, items)
    rig.m.w32(items + LEVEL_COUNT_OFFSET, len(flags))
    for index, flag in enumerate(flags):
        rig.m.w32(rig.table + index * ROW_SIZE + 0x8C, flag)
    sentinel = 0x12345678
    rig.m.w32(PORTAL_ARRAY, sentinel)
    result = rig.go(FUNCTION)
    require(result[0] == "ret", f"LevelDefs loader: {result}; {rig.log}")
    expected = [index for index, flag in enumerate(flags) if flag]
    require((rig.m.r32(LEVELS), rig.m.r32(PORTAL_COUNT)) == (rig.table, len(expected)),
            f"LevelDefs table or portal count for {flags}")
    pointer = rig.m.r32(PORTAL_ARRAY)
    require(bool(expected) == (pointer != sentinel),
            f"LevelDefs portal allocation gate for {flags}")
    if expected:
        actual = [rig.m.r32(pointer + 4 * index) for index in range(len(expected))]
        require(actual == expected, f"LevelDefs portal row indices {actual} != {expected}")
    allocations = [entry for entry in rig.log if entry[0] == "alloc"]
    wanted = [("alloc", 0, len(expected) * 4, FILE, 0x16C, 0)] if expected else []
    require(allocations == wanted, f"LevelDefs portal allocation {allocations} != {wanted}")


def main():
    """Cover zero, absent, singleton and multiple portal flags."""
    for flags in ((), (0,), (1,), (0, 1, 0, 1), (1, 1, 0, 1, 0)):
        check(flags)
    print("ok: FUN_0061de00 five integrated LevelDefs portal-array cases")


if __name__ == "__main__":
    main()
