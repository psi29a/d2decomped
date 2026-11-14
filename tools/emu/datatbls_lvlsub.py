# SPDX-License-Identifier: GPL-3.0-or-later
"""Check LvlTbls.cpp FUN_0061f500's Type index on synthetic LvlSub rows.

Usage: D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_lvlsub.py

CompileTxt and the file-name normalizer are hooked; the index builder runs in
the original game.exe. No MPQ or DS1 file is read.
"""

import struct

import _monster_world as mw
from _monster_world import require
from monster_loaders import Rig


FUNCTION = 0x61F500
STRIDE = 0x15C
ROWS = 0x96CA10
COUNT = 0x96CA14
TYPE_INDEX = 0x96CA18
FILE = ".\\DATATBLS\\LvlTbls.cpp"


def check(types):
    """Verify Type's max-sized index and change-point semantics."""
    rig = Rig()
    rig.rows = len(types)
    rig.table = rig.m.alloc(max(STRIDE, STRIDE * len(types)))
    for index, row_type in enumerate(types):
        rig.write(rig.table + index * STRIDE, struct.pack("<I", row_type))
    rig.logger(0x61E4A0, 0, "file-path", regs=())
    result = rig.go(FUNCTION, 0, ecx=0, edx=0)
    require(result[0] == "ret", f"LvlSub loader did not return: {result}; {rig.log}")
    require((rig.m.r32(ROWS), rig.m.r32(COUNT)) == (rig.table, len(types)),
            "LvlSub table or count differs")
    expected = [0] * (max(types) + 1 if types else 0)
    previous = 0
    for index, row_type in enumerate(types):
        if row_type != previous:
            expected[row_type] = index
            previous = row_type
    index_ptr = rig.m.r32(TYPE_INDEX)
    if types and max(types):
        require(index_ptr != 0, "missing nonzero Type index")
        actual = [rig.m.r32(index_ptr + 4 * index) for index in range(len(expected))]
        require(actual == expected, f"LvlSub Type index {actual} != {expected}")
    else:
        require(index_ptr == 0, "unexpected zero Type index")
    allocations = [entry for entry in rig.log if entry[0] == "alloc"]
    expected_alloc = [("alloc", 0, 4 * len(expected), FILE, 0x3A6, 0)] if index_ptr else []
    require(allocations == expected_alloc,
            f"LvlSub index allocation {allocations} != {expected_alloc}")
    normalizations = [entry for entry in rig.log if entry[0] == "file-path"]
    require(len(normalizations) == len(types),
            f"LvlSub normalized {len(normalizations)} rows, expected {len(types)}")


def check_cache(flag_2, flag_3, expansion, valid):
    """Exercise the cache flags, expansion gate, and missing-data fatal path."""
    rig = Rig()
    rig.rows = 3
    rig.expansion = expansion
    rig.table = rig.m.alloc(3 * STRIDE)
    for index, row_expansion in enumerate((0, 1, 0)):
        rig.m.w32(rig.table + index * STRIDE + 0x158, row_expansion)
    rig.logger(0x61E4A0, 0, "file-path", regs=())
    cached = rig.m.alloc(0x50)
    rig.m.w32(cached + 0x4C, int(valid))

    def allocate_cache(current):
        """Model the Storm allocator at the boundary of this table loader."""
        size = current.mu.reg_read(mw.REGISTERS["ecx"])
        rig.log.append(("alloc-cache", size, current.arg(0), current.arg(1)))
        return current.alloc(size)

    def acquire(current):
        """Return a scripted DS1 through the real callee's ECX output slot."""
        output = current.mu.reg_read(mw.REGISTERS["ecx"])
        rig.log.append(("ds1", output, current.arg(0)))
        current.w32(output, cached)
        return 0

    rig.m.hook(0x665F40, acquire, 1)
    rig.m.hook(0x40B380, allocate_cache, 2)
    result = rig.go(FUNCTION, flag_3, ecx=0, edx=flag_2)
    gated = bool(flag_2 or flag_3)
    expected_calls = 3 if expansion else 2
    calls = [entry for entry in rig.log if entry[0] == "ds1"]
    if not gated:
        require(result[0] == "ret" and not calls and rig.m.r32(0x96CA24) == 0,
                f"LvlSub unflagged cache path: {result}; {rig.log}")
        return
    require(rig.m.r32(0x96CA24) != 0,
            f"LvlSub cache array not allocated: flags {flag_2},{flag_3}, expansion {expansion}; {result}; {rig.log}")
    allocations = [entry for entry in rig.log if entry[0] == "alloc-cache"]
    require(allocations == [("alloc-cache", 3 * 4, 0x3B6, 0)],
            f"LvlSub cache allocation {allocations}")
    if valid:
        require(result[0] == "ret" and len(calls) == expected_calls,
                f"LvlSub cache gate: {result}; {calls}")
    else:
        require(result == ("halt", 0x3C8) and len(calls) == 1,
                f"LvlSub bad DS1 fatal: {result}; {calls}")


def main():
    """Cover empty, one-type, increasing, gapped, and out-of-order rows."""
    cases = ((), (0,), (1,), (0, 2, 2), (1, 1, 4), (2, 0, 2), (0, 3, 1, 3, 0))
    for types in cases:
        check(types)
    for flag_2, flag_3, expansion in ((0, 0, 0), (0, 0, 1), (1, 0, 0),
                                       (1, 0, 1), (0, 1, 0), (0, 1, 1)):
        check_cache(flag_2, flag_3, expansion, True)
        if flag_2 or flag_3:
            check_cache(flag_2, flag_3, expansion, False)
    print(f"ok: FUN_0061f500 {len(cases)} Type indices and 10 cache-gate cases")


if __name__ == "__main__":
    main()
