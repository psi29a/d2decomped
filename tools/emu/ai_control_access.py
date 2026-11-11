# SPDX-License-Identifier: GPL-3.0-or-later
"""Check two AiGeneral.cpp accessors in stock 1.14d; no MPQs are read.

    uv run python ai_control_access.py --exe /path/to/game.exe

FUN_0058ebc0 allocates and initialises a control. FUN_0058ec50 reads scratch
words by selector. FUN_0058f000 returns the
address of control +0x38, including the literal 0x38 for a missing control.
Synthetic unit, monster-data and control records exercise each pointer gate.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
SCRATCH = 0x58EC50
FIELD_ADDRESS = 0x58F000
ALLOCATE = 0x58EBC0


def check(machine, address, expected, *args, **registers):
    """Require the expected EAX and an actual return within the limit."""
    actual = machine.call(address, *args, limit=1000, **registers)
    if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or actual != expected:
        raise AssertionError(f"{address:08x}: expected {expected:08x}, got {actual:08x}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    game_address = machine.alloc(0x100)
    game = machine.view("game", game_address)
    pool = machine.alloc(0x10)
    game.pool = pool
    allocated = machine.alloc(0x40)
    machine.mu.mem_write(allocated, bytes([0xA5]) * 0x40)
    allocation_calls = []

    def allocate(current):
        allocation_calls.append((current.mu.reg_read(UC_X86_REG_ECX),
                                 current.mu.reg_read(UC_X86_REG_EDX),
                                 current.cstr(current.arg(0)), current.arg(1), current.arg(2)))
        return allocated

    machine.hook(0x40B430, allocate, 3)
    check(machine, ALLOCATE, allocated, ecx=game_address)
    if allocation_calls != [(pool, 0x40, ".\\Ai\\AiGeneral.cpp", 0x1B, 0)]:
        raise AssertionError(f"unexpected allocation calls: {allocation_calls!r}")
    expected = bytearray(0x40)
    for offset, value in ((0x0C, 0xFFFFFFFF), (0x10, 1), (0x2C, 0xFFFFFFFF), (0x30, 1)):
        expected[offset:offset + 4] = value.to_bytes(4, "little")
    if machine.read(allocated, 0x40) != expected:
        raise AssertionError("AI control initialization differs")
    unit_address = machine.alloc(0x100)
    data_address = machine.alloc(0x80)
    control_address = machine.alloc(0x40)
    unit = machine.view("unit", unit_address)
    data = machine.view("monster_data", data_address)
    control = machine.view("ai_control", control_address)
    control.scratch_one, control.scratch_two, control.scratch_three = 0x12345678, 0x87654321, 0xDEADBEEF

    for selector in (0, 1, 2, 3, 4, 0xFFFFFFFF):
        check(machine, SCRATCH, 0, ecx=0, edx=selector)
    check(machine, FIELD_ADDRESS, 0x38, 0)
    for kind, data_pointer, control_pointer in ((0, data_address, control_address),
                                                (1, 0, control_address),
                                                (1, data_address, 0)):
        unit.type, unit.data, data.ai_control = kind, data_pointer, control_pointer
        for selector in (0, 1, 2, 3, 4, 0xFFFFFFFF):
            check(machine, SCRATCH, 0, ecx=unit_address, edx=selector)
        check(machine, FIELD_ADDRESS, 0x38, unit_address)

    unit.type, unit.data, data.ai_control = 1, data_address, control_address
    for selector, expected in ((0, 0), (1, 0x12345678), (2, 0x87654321),
                               (3, 0xDEADBEEF), (4, 0), (0xFFFFFFFF, 0)):
        check(machine, SCRATCH, expected, ecx=unit_address, edx=selector)
    check(machine, FIELD_ADDRESS, control_address + 0x38, unit_address)
    print("ok: control allocation, scratch selector and control-field address gates")


if __name__ == "__main__":
    main()
