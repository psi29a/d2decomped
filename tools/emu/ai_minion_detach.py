# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_0058f440 detachment and FUN_0058f4d0 list replacement in 1.14d.

    uv run python ai_minion_detach.py --exe /path/to/game.exe

The flag reader, attach helper and allocator are hooked; no MPQs are read.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    require(hashlib.sha256(args.exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    unit_address, data_address, control_address = machine.alloc(0x100), machine.alloc(0x80), machine.alloc(0x40)
    unit, data = machine.view("unit", unit_address), machine.view("monster_data", data_address)
    control = machine.view("ai_control", control_address)
    unit.data, data.ai_control = data_address, control_address
    outputs = [machine.alloc(4) for _ in range(5)]
    calls = []

    def flag(current):
        calls.append((current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
        return 0xA0 + calls[-1][1]

    machine.hook(0x5DD220, flag, 0)
    for pointer, kind, owner_type, success in ((0, 1, 1, False), (unit_address, 0, 1, False),
                                                (unit_address, 1, 0, False), (unit_address, 1, 1, True)):
        unit.type, control.owner_id, control.owner_type, control.minions = kind, 123, owner_type, 0x12345678
        for output in outputs:
            machine.w32(output, 0xCCCCCCCC)
        calls.clear()
        machine.call(0x58F440, *outputs[1:], ecx=pointer, edx=outputs[0], limit=1000)
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "detach did not return")
        values = [machine.r32(output) for output in outputs]
        if success:
            require(values == [123, 1, 0xA1, 0xA2, 0x12345678], f"outputs differ: {values!r}")
            require(calls == [(control_address, 1), (control_address, 2)], "flag calls differ")
            require((control.owner_id, control.minions) == (0xFFFFFFFF, 0), "control was not detached")
        else:
            require(values == [0xFFFFFFFF] + [0xCCCCCCCC] * 4 and not calls, "invalid-unit output differs")
            require((control.owner_id, control.minions) == (123, 0x12345678), "early return mutated control")
    game_address, pool = machine.alloc(0x100), machine.alloc(0x10)
    machine.view("game", game_address).pool = pool
    nodes = [machine.alloc(8) for _ in range(2)]
    machine.view("minion_node", nodes[0]).next = nodes[1]
    machine.view("minion_node", nodes[1]).next = 0
    replacement = machine.alloc(8)
    calls.clear()

    def free(current):
        calls.append(("free", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      current.cstr(current.arg(0)), current.arg(1), current.arg(2)))
        return 0

    def attach(current):
        calls.append(("attach", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      *(current.arg(index) for index in range(4))))
        return 0

    machine.hook(0x40B480, free, 3)
    machine.hook(0x58F030, attach, 4)
    for pointer, kind, success in ((0, 1, False), (unit_address, 0, False), (unit_address, 1, True)):
        unit.type, control.minions = kind, nodes[0]
        calls.clear()
        machine.call(0x58F4D0, 11, 22, 33, 44, replacement, ecx=game_address, edx=pointer, limit=10000)
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "list replacement did not return")
        if success:
            expected = [("free", pool, node, ".\\Ai\\AiGeneral.cpp", 0x246, 0) for node in nodes]
            expected.append(("attach", game_address, unit_address, 11, 22, 33, 44))
            require(calls == expected, f"list replacement calls differ: {calls!r}")
            require(control.minions == replacement, "replacement head not installed")
        else:
            require(not calls and control.minions == nodes[0], "invalid unit changed list")
    print("ok: minion-list detachment, replacement and invalid owner paths")


if __name__ == "__main__":
    main()
