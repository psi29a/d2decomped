# SPDX-License-Identifier: GPL-3.0-or-later
"""Check LvlTbls.cpp FUN_0061ebb0's six-file walk and DS1 cache gate.

Usage: D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_lvlprest.py

CompileTxt, Storm allocation and DS1 acquisition are hooked. Filename
normalization and the six-file walk run in game.exe on synthetic rows.
"""

import _monster_world as mw
from _monster_world import require
from monster_loaders import Rig


FUNCTION = 0x61EBB0
STRIDE = 0x1B0
PREST = 0x96C894
COUNT = 0x96C898
CACHE = 0x96CA20
NAMES = (b"a/b", b"x", b"", b"c/d", b"", b"e")


def check(flag, expansion):
    """Compare the expansion predicate, cache slots and filename rewrites."""
    rig = Rig()
    rig.rows = 3
    rig.expansion = expansion
    rig.table = rig.m.alloc(3 * STRIDE)
    for row, row_expansion in enumerate((0, 1, 0)):
        base = rig.table + STRIDE * row
        rig.m.w32(base + 0x20, row_expansion)
        for index, name in enumerate(NAMES):
            rig.write(base + 0x44 + 0x3C * index, name + b"\0")

    def allocate_cache(current):
        """Hand back a zeroed six-pointer cache record per row."""
        size = current.mu.reg_read(mw.REGISTERS["ecx"])
        rig.log.append(("alloc-cache", size, current.arg(0), current.arg(1)))
        return current.alloc(size)

    def acquire(current):
        """Record the output slot and normalized file name for each DS1."""
        output = current.mu.reg_read(mw.REGISTERS["ecx"])
        rig.log.append(("ds1", output, rig.m.cstr(current.arg(0))))
        current.w32(output, 0x120000 + len(rig.log))
        return 0

    rig.m.hook(0x40B380, allocate_cache, 2)
    rig.m.hook(0x665F40, acquire, 1)
    result = rig.go(FUNCTION, ecx=0, edx=flag)
    require(result[0] == "ret", f"LvlPrest loader: {result}; {rig.log}")
    require((rig.m.r32(PREST), rig.m.r32(COUNT)) == (rig.table, 3),
            "LvlPrest table or count differs")
    eligible = (0, 1, 2) if expansion else (0, 2)
    allocations = [entry for entry in rig.log if entry[0] == "alloc-cache"]
    require(allocations == ([("alloc-cache", 3 * 0x18, 0x2A7, 0)] if flag else []),
            f"LvlPrest cache allocation {allocations}")
    cache = rig.m.r32(CACHE)
    require(bool(cache) == bool(flag), "LvlPrest cache pointer gate")
    calls = [entry for entry in rig.log if entry[0] == "ds1"]
    expected_slots = [cache + row * 0x18 + index * 4 for row in eligible for index in (0, 3)] if flag else []
    require([entry[1] for entry in calls] == expected_slots,
            f"LvlPrest DS1 acquisition slots {calls} != {expected_slots}")
    for row in range(3):
        for index, name in enumerate(NAMES):
            current = rig.m.cstr(rig.table + row * STRIDE + 0x44 + index * 0x3C)
            if row not in eligible:
                require(current == name.decode("ascii"), f"LvlPrest excluded row {row} file {index}")
            elif name in (b"x", b"e", b""):
                require(current == name.decode("ascii"), f"LvlPrest short name {row}:{index}")
            else:
                require("/" not in current and "\\" in current,
                        f"LvlPrest path slash conversion {row}:{index}: {current!r}")


def main():
    """Cover cache flag off/on in both classic and expansion modes."""
    for flag in (0, 1):
        for expansion in (0, 1):
            check(flag, expansion)
    print("ok: FUN_0061ebb0 four LvlPrest file and cache-gate cases")


if __name__ == "__main__":
    main()
