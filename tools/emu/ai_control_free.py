# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify AiGeneral.cpp minion-list and control teardown in stock 1.14d.

    uv run python ai_control_free.py --exe /path/to/game.exe

The debug free and optional-record release are hooked; no MPQs are read.
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
    game_address, control_address = machine.alloc(0x100), machine.alloc(0x40)
    pool, preset = machine.alloc(0x10), machine.alloc(0x10)
    machine.view("game", game_address).pool = pool
    control = machine.view("ai_control", control_address)
    nodes = [machine.alloc(8) for _ in range(3)]
    for current, following in zip(nodes, nodes[1:] + [0]):
        machine.view("minion_node", current).next = following
    calls = []

    def capture_free(current):
        calls.append(("free", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      current.cstr(current.arg(0)), current.arg(1), current.arg(2)))
        return 0

    def release_record(current):
        calls.append(("record", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
        return 0

    machine.hook(0x40B480, capture_free, 3)
    machine.hook(0x666130, release_record, 0)

    def run(address, **registers):
        machine.call(address, limit=10000, **registers)
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "call did not return")

    source = ".\\Ai\\AiGeneral.cpp"
    def free_call(address, line):
        return ("free", pool, address, source, line, 0)

    run(0x58F350, ecx=game_address, edx=0)
    run(0x58F810, ecx=game_address, edx=0)
    require(not calls, "null input invoked cleanup")
    run(0x58F350, ecx=game_address, edx=nodes[0])
    require(calls == [free_call(node, 0x246) for node in nodes], "minion free order differs")
    calls.clear()
    run(0x58F810, ecx=game_address, edx=control_address)
    require(calls == [free_call(control_address, 0x19D)], "empty control cleanup differs")
    calls.clear()
    control.minions, control.preset_record = nodes[0], preset
    command = machine.alloc(0x1C)
    command_node = machine.view("ai_command", command)
    command_node.next = command_node.previous = command
    control.command_current = control.command_head = command
    run(0x58F810, ecx=game_address, edx=control_address)
    expected = ([("record", pool, preset)] + [free_call(node, 0x246) for node in nodes]
                + [free_call(command, 0xC4), free_call(control_address, 0x19D)])
    require(calls == expected, f"populated control cleanup differs: {calls!r}")
    require(control.command_current == control.command_head == 0, "ring pointers not cleared")
    print("ok: null and populated AI control teardown, minion free order")


if __name__ == "__main__":
    main()
