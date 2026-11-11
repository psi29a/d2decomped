# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify AiGeneral.cpp command-ring insertion and release in game.exe.

    uv run python ai_command_ring.py --exe /path/to/game.exe

The pool allocator and free function are intercepted; no MPQs are read.
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
    game_address, unit_address = machine.alloc(0x100), machine.alloc(0x100)
    data_address, control_address = machine.alloc(0x80), machine.alloc(0x40)
    pool = machine.alloc(0x10)
    game, unit = machine.view("game", game_address), machine.view("unit", unit_address)
    data = machine.view("monster_data", data_address)
    control = machine.view("ai_control", control_address)
    game.pool = pool
    allocations, frees = [], []

    def allocate(current):
        allocations.append((current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                            current.cstr(current.arg(0)), current.arg(1), current.arg(2)))
        address = current.alloc(0x1C)
        current.mu.mem_write(address, bytes([0xA5]) * 0x1C)
        return address

    def free(current):
        frees.append((current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      current.cstr(current.arg(0)), current.arg(1), current.arg(2)))
        return 0

    machine.hook(0x40B430, allocate, 3)
    machine.hook(0x40B480, free, 3)

    def run(address, **kwargs):
        result = machine.call(address, limit=10000, **kwargs)
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "call did not return")
        return result

    require(run(0x58EC90, ecx=game_address, edx=0) == 0, "null unit")
    for kind, data_pointer, control_pointer in ((0, data_address, control_address),
                                                (1, 0, control_address), (1, data_address, 0)):
        unit.type, unit.data, data.ai_control = kind, data_pointer, control_pointer
        require(run(0x58EC90, ecx=game_address, edx=unit_address) == 0, "invalid unit gate")
    require(not allocations, "invalid unit allocated a node")
    unit.type, unit.data, data.ai_control = 1, data_address, control_address
    nodes = []
    for count in range(1, 4):
        node_address = run(0x58EC90, ecx=game_address, edx=unit_address)
        nodes.append(node_address)
        node = machine.view("ai_command", node_address)
        require(machine.read(node_address + 8, 0x14) == bytes(0x14), "node payload not zero")
        require(control.command_current == nodes[0], "current pointer changed")
        require(control.command_head == nodes[0 if count == 1 else 1], "head pointer differs")
        require(node.next == nodes[0], "new node next differs")
        require(node.previous == (nodes[-2] if count > 1 else nodes[0]), "new node previous differs")
    for index, address in enumerate(nodes):
        node = machine.view("ai_command", address)
        require(node.next == nodes[(index + 1) % len(nodes)], "next link differs")
        require(node.previous == nodes[(index - 1) % len(nodes)], "previous link differs")
    require(allocations == [(pool, 0x1C, ".\\Ai\\AiGeneral.cpp", 0x65, 0)] * 3,
            "allocator arguments differ")
    run(0x58ED80, regs={"esi": control_address, "edi": game_address})
    require(control.command_current == control.command_head == 0, "ring pointers not cleared")
    require(frees == [(pool, node, ".\\Ai\\AiGeneral.cpp", 0xC4, 0) for node in nodes],
            f"free calls differ: {frees!r}")
    run(0x58ED80, regs={"esi": control_address, "edi": game_address})
    require(len(frees) == 3, "empty ring freed again")
    print("ok: command-ring gates, insertion, links and release")


if __name__ == "__main__":
    main()
