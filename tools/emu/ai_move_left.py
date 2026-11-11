# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005ea080's one-time leftward action and termination.

    uv run python ai_move_left.py --exe /path/to/game.exe

The action and dynamic-coordinate helpers are hooked; no MPQs are read.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    unit_address = machine.alloc(0x100)
    path_address = machine.alloc(0x40)
    control_address = machine.alloc(0x40)
    think_address = machine.alloc(0x20)
    unit = machine.view("unit", unit_address)
    path = machine.view("static_path", path_address)
    control = machine.view("ai_control", control_address)
    machine.view("ai_think", think_address).control = control_address
    calls = []

    def action(current):
        calls.append(tuple(current.arg(index) for index in range(3)))
        return 0

    machine.hook(0x5DDFC0, action, 3)
    machine.hook(0x6488C0, lambda current: 42, 1)
    machine.hook(0x648900, lambda current: 71, 1)
    unit.flags, path.x, path.y = 0x51, 42, 71
    for kind, pointer, x, y in ((1, path_address, 42, 71), (1, 0, 0, 0),
                                 (2, path_address, 42, 71), (4, path_address, 42, 71),
                                 (5, path_address, 42, 71)):
        unit.type, unit.path, control.scratch_two = kind, pointer, 0
        machine.call(0x5EA080, think_address, ecx=0x1234, edx=unit_address, limit=1000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError("initial action did not return")
        if calls.pop() != (4, (x - 10) & 0xFFFFFFFF, y) or control.scratch_two != 1:
            raise AssertionError("initial action differs")
    for pointer in (0, unit_address):
        control.scratch_two = 1
        machine.call(0x5EA080, think_address, ecx=0x1234, edx=pointer, limit=1000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or calls.pop() != (0, 0, 0):
            raise AssertionError("termination action differs")
    if unit.flags != 0x20051 or calls:
        raise AssertionError("termination flag or action count differs")
    print("ok: one-time action, coordinate selection and termination")


if __name__ == "__main__":
    main()
