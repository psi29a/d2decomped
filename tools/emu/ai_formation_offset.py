# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005e5730's formation offsets and leader seed draw.

    uv run python ai_formation_offset.py --exe /path/to/game.exe

The owner lookup and dynamic coordinate getters are hooked; no MPQs are read.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    unit_address, owner_address = machine.alloc(0x100), machine.alloc(0x100)
    unit_path, owner_path = machine.alloc(0x40), machine.alloc(0x40)
    think_address, control_address = machine.alloc(0x40), machine.alloc(0x40)
    unit, owner = machine.view("unit", unit_address), machine.view("unit", owner_address)
    control = machine.view("ai_control", control_address)
    machine.view("ai_think", think_address).control = control_address
    machine.view("static_path", unit_path).x, machine.view("static_path", unit_path).y = 120, 85
    machine.view("static_path", owner_path).x, machine.view("static_path", owner_path).y = 100, 90
    values = {unit_path: (120, 85), owner_path: (100, 90)}
    calls = []
    scripted = {"owner": owner_address}

    def find_owner(current):
        calls.append(("owner", current.mu.reg_read(UC_X86_REG_ECX)))
        return scripted["owner"]

    def coordinate(axis):
        def callback(current):
            address = current.arg(0)
            calls.append(("xy"[axis], address))
            return values[address][axis]
        return callback

    machine.hook(0x58F0D0, find_owner, 0)
    machine.hook(0x6488C0, coordinate(0), 1)
    machine.hook(0x648900, coordinate(1), 1)
    cases = ((1, 1, unit_path, owner_path, False), (2, 4, unit_path, owner_path, False),
             (1, 1, 0, owner_path, False), (1, 1, unit_path, 0, False),
             (1, 1, unit_path, owner_path, True))
    for unit_type, owner_type, unit_ptr, owner_ptr, is_owner in cases:
        unit.type, unit.path = unit_type, unit_ptr
        owner.type, owner.path = owner_type, owner_ptr
        unit.seed_low, unit.seed_high = 0x12345678, 666
        control.scratch_one, control.scratch_two, control.scratch_three = 0, 0, 77
        scripted["owner"] = unit_address if is_owner else owner_address
        calls.clear()
        machine.call(0x5E5730, think_address, ecx=0x1234, edx=unit_address, limit=10000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError("formation offset did not return")
        unit_xy = values[unit_ptr] if unit_ptr else (0, 0)
        owner_xy = unit_xy if is_owner else values[owner_ptr] if owner_ptr else (0, 0)
        expected_x = (unit_xy[0] - owner_xy[0]) & 0xFFFFFFFF
        expected_y = (unit_xy[1] - owner_xy[1]) & 0xFFFFFFFF
        if (control.scratch_one, control.scratch_two) != (expected_x, expected_y):
            raise AssertionError("formation offset differs")
        if is_owner:
            stepped = 0x12345678 * 0x6AC690C5 + 666
            expected_seed = (stepped & 0xFFFFFFFF, stepped >> 32)
            expected_third = (stepped & 0xFFFFFFFF) % 100
        else:
            expected_seed, expected_third = (0x12345678, 666), 77
        if (unit.seed_low, unit.seed_high, control.scratch_three) != (*expected_seed, expected_third):
            raise AssertionError("formation seed or third scratch differs")
        expected_calls = [("owner", unit_address)]
        actual_owner_path, actual_owner_type = (unit_ptr, unit_type) if is_owner else (owner_ptr, owner_type)
        for kind, address in ((actual_owner_type, actual_owner_path), (unit_type, unit_ptr)):
            if kind not in (2, 4, 5) and address:
                expected_calls.extend((("x", address), ("y", address)))
        if calls != expected_calls:
            raise AssertionError(f"unexpected calls {calls!r} != {expected_calls!r}")
    print(f"ok: {len(cases)} formation offset and seed cases")


if __name__ == "__main__":
    main()
