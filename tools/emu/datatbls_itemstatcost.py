# SPDX-License-Identifier: GPL-3.0-or-later
"""Check ItemTbls.cpp FUN_00637a00's bit width and sorted priority index.

Usage: D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_itemstatcost.py

CompileTxt and link creation are hooked. The original row initialization,
operation and parameter-stat chains, signed priority sorting, bit-width
clamp, and fatal count guard run on synthetic tables.
"""

import struct

from _monster_world import require
from monster_loaders import Rig


FUNCTION = 0x637A00
STRIDE = 0x144
BITS = 0x96C89C
MASK = 0x96C8A0
SORTED = 0x96C808
SORTED_COUNT = 0x96C80C


def check(priorities, bits):
    """Compare the bit-width clamp, row init, and sorted nonempty priorities."""
    rig = Rig()
    rig.rows = len(priorities)
    rig.table = rig.m.alloc(max(STRIDE, STRIDE * len(priorities)))
    rig.links = [rig.m.alloc(4)]
    rig.m.w32(rig.table + 0x140, bits)
    for index, priority in enumerate(priorities):
        row = rig.table + STRIDE * index
        rig.write(row + 0x34, struct.pack("<h", priority))
        rig.write(row + 0x36, bytes((int(priority != 0),)))
    result = rig.go(FUNCTION)
    require(result[0] == "ret", f"itemstatcost loader: {result}; {rig.log}")
    effective_bits = bits if 1 <= bits <= 8 else 6
    require((rig.m.r32(BITS), rig.m.r32(MASK)) ==
            (effective_bits, (1 << effective_bits) - 1),
            f"itemstatcost bit width {bits} -> {rig.m.r32(BITS)}")
    for index in range(len(priorities)):
        row = rig.table + STRIDE * index
        require(rig.m.read(row + 0x5E, 0x80) == bytes([0xFF]) * 0x80,
                f"itemstatcost row {index} chain init")
    sorted_rows = sorted((index for index, priority in enumerate(priorities)
                          if priority != 0),
                         key=lambda index: priorities[index])
    require(rig.m.r32(SORTED_COUNT) == len(sorted_rows),
            f"itemstatcost sorted count {rig.m.r32(SORTED_COUNT)}")
    address = rig.m.r32(SORTED)
    require([rig.m.r16(address + 2 * index) for index in range(len(sorted_rows))] == sorted_rows,
            f"itemstatcost priority order, priorities {priorities}")


def check_limit(count):
    """A count greater than 511 must halt before row processing."""
    rig = Rig()
    rig.rows = count
    rig.table = rig.m.alloc(STRIDE)
    rig.links = [rig.m.alloc(4)]
    result = rig.go(FUNCTION)
    require(result == ("halt", 0x6B6), f"itemstatcost count {count}: {result}")


def check_op(opcode, target):
    """Exercise one operation link, its target range and op 4/5 marker."""
    rig = Rig()
    rig.rows = 3
    rig.table = rig.m.alloc(3 * STRIDE)
    rig.links = [rig.m.alloc(4)]
    rig.write(rig.table + 0x54, bytes((opcode,)))
    rig.write(rig.table + 0x56, struct.pack("<H", target))
    for offset in (0x58, 0x5A, 0x5C):
        rig.write(rig.table + offset, b"\xff\xff")
    result = rig.go(FUNCTION)
    require(result[0] == "ret", f"itemstatcost op {opcode}, target {target}: {result}; {rig.log}")
    active = 1 <= opcode <= 13 and target < 3
    require(rig.m.read(rig.table + 0x54, 1)[0] == (opcode if 1 <= opcode <= 13 else 0),
            f"itemstatcost op {opcode} was not clamped")
    if target < 3:
        require(rig.m.read(rig.table + STRIDE * target + 0x51, 1)[0] == int(active),
                f"itemstatcost op {opcode} target flag")
        require(rig.m.r16(rig.table + STRIDE * target + 0x5E) == (0 if active else 0xFFFF),
                f"itemstatcost op {opcode} target chain")
    require(rig.m.read(rig.table + 0x53, 1)[0] == int(active and opcode in (4, 5)),
            f"itemstatcost op {opcode} source marker")


def check_parameter_stats():
    """Exercise all three parameter-stat slots and special targets 7/9/11."""
    rig = Rig()
    rig.rows = 12
    rig.table = rig.m.alloc(12 * STRIDE)
    rig.links = [rig.m.alloc(4)]
    rig.write(rig.table + 0x54, b"\x01")
    rig.write(rig.table + 0x56, struct.pack("<4H", 1, 7, 9, 11))
    result = rig.go(FUNCTION)
    require(result[0] == "ret", f"itemstatcost param stats: {result}; {rig.log}")
    require(rig.m.r16(rig.table + STRIDE + 0x5E) == 0,
            "itemstatcost operation target 1 lacks source link")
    for target in (7, 9, 11):
        row = rig.table + STRIDE * target
        require(rig.m.read(row + 0x51, 2) == b"\x00\x01",
                f"itemstatcost parameter target {target} flags {rig.m.read(row + 0x51, 2)!r}; "
                f"record {rig.m.read(row + 0xDE, 8)!r}; source {rig.m.read(rig.table + 4, 2)!r}")
        require((rig.m.r16(row + 0xDE), rig.m.r16(row + 0xE0),
                 rig.m.read(row + 0xE2, 2)) == (1, 0, b"\x01\x00"),
                f"itemstatcost parameter target {target} back-reference")
    require(rig.m.read(rig.table + 4, 2) == b"\xe0\x01",
            f"itemstatcost source flags {rig.m.read(rig.table + 4, 2)}")


def main():
    """Exercise all bit-width boundary cases and signed priority ordering."""
    for bits in (0, 1, 3, 8, 9, 255):
        check((300, 0, -10, 512, 1), bits)
    for count in (512, 513):
        check_limit(count)
    for opcode, target in ((0, 1), (1, 1), (4, 1), (5, 1), (13, 1), (14, 1), (1, 3), (4, 3)):
        check_op(opcode, target)
    check_parameter_stats()
    print("ok: FUN_00637a00 six bit-width/priority, eight op-link, one parameter-stat and two fatal cases")


if __name__ == "__main__":
    main()
