# SPDX-License-Identifier: GPL-3.0-or-later
"""Check ItemTbls.cpp FUN_006342b0's unique item row processing and limit.

Usage: D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_uniqueitems.py

CompileTxt, link insertion and string lookup are hooked. The row numbers,
name destinations, missing-string fallback, and fatal count limit run in the
original executable on synthetic rows.
"""

from _monster_world import require
from monster_loaders import Rig


FUNCTION = 0x6342B0
ROW_SIZE = 0x14C
TABLE = 0x96C854
COUNT = 0x96C858
LINK = 0x96C850


def check(values):
    """Verify link and string calls and every derived word for a short table."""
    rig = Rig()
    rig.rows = len(values)
    rig.table = rig.m.alloc(max(ROW_SIZE, ROW_SIZE * len(values)))
    link = rig.m.alloc(4)
    rig.links = [link]
    rig.ids = list(values)

    def add_name(current):
        """Record link insertion without running Excel's separate hash table."""
        rig.log.append(("add-name", current.arg(0), current.arg(1)))
        return 0

    rig.m.hook(0x6BD500, add_name, 2)
    result = rig.go(FUNCTION)
    require(result[0] == "ret", f"unique item loader: {result}; {rig.log}")
    require((rig.m.r32(TABLE), rig.m.r32(COUNT), rig.m.r32(LINK)) ==
            (rig.table, len(values), link), "unique item table slots")
    inserted = [entry for entry in rig.log if entry[0] == "add-name"]
    lookups = [entry for entry in rig.log if entry[0] == "lookup"]
    expected_names = [rig.table + ROW_SIZE * index + 2 for index in range(len(values))]
    require(inserted == [("add-name", link, pointer) for pointer in expected_names],
            f"unique item link insertions {inserted}")
    require([entry[1] for entry in lookups] == expected_names,
            f"unique item string lookups {lookups}")
    for index, string_id in enumerate(values):
        row = rig.table + ROW_SIZE * index
        require(rig.m.r16(row) == index and rig.m.r16(row + 0x22) == (string_id or 0x1507),
                f"unique item row {index} index or string id")


def check_limit(count):
    """The count guard must fire before it walks unmaterialized rows."""
    rig = Rig()
    rig.rows = count
    rig.table = rig.m.alloc(ROW_SIZE)
    rig.links = [rig.m.alloc(4)]
    result = rig.go(FUNCTION)
    require(result == ("halt", 0x37C), f"unique item count {count}: {result}")
    require(rig.m.r32(COUNT) == count, "unique item fatal count was changed")


def main():
    """Cover empty/mixed rows and both first invalid counts."""
    for values in ((), (0,), (0x100,), (0, 0x123, 0x8000), (0xFFFF, 0, 1, 0)):
        check(values)
    for count in (0x7FFF, 0x8000):
        check_limit(count)
    print("ok: FUN_006342b0 five populated cases and two fatal limits")


if __name__ == "__main__":
    main()
