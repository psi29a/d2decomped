# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005e7f80's command-8 minion retreat branches.

    uv run python ai_minion_retreat.py --exe /path/to/game.exe

Command access, group scan, broadcast, unlink, state switch and idle are
scripted; no MPQs are read.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    game_address, unit_address = machine.alloc(0x200), machine.alloc(0x100)
    think_address, control_address, command_address = machine.alloc(0x40), machine.alloc(0x40), machine.alloc(0x20)
    think = machine.view("ai_think", think_address)
    command = machine.view("ai_command", command_address)
    think.control = control_address
    calls = []
    scripted = {"command": 0, "group": 0, "signal": 0}

    def get_command(current):
        calls.append(("command", current.mu.reg_read(UC_X86_REG_ECX)))
        return scripted["command"]

    def scan(current):
        calls.append(("scan", current.mu.reg_read(UC_X86_REG_ECX), current.arg(0), current.arg(1)))
        current.w32(current.mu.reg_read(UC_X86_REG_EDX), scripted["group"])
        return 0

    def signal(current):
        calls.append(("signal", current.mu.reg_read(UC_X86_REG_ECX)))
        return scripted["signal"]

    def broadcast(current):
        packet = machine.view("ai_command", current.arg(0))
        calls.append(("broadcast", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX), packet.command))
        return 0

    def unlink(current):
        calls.append(("unlink", current.mu.reg_read(UC_X86_REG_ECX)))
        return 0

    def switch(current):
        calls.append(("switch", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      current.arg(0), current.arg(1)))
        return 0

    def idle(current):
        calls.append(("idle", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX), current.arg(0)))
        return 0

    machine.hook(0x58EE80, get_command, 0)
    machine.hook(0x58F380, scan, 2)
    machine.hook(0x5DD2B0, signal, 0)
    machine.hook(0x58F730, broadcast, 1)
    machine.hook(0x58F160, unlink, 0)
    machine.hook(0x5B0E00, switch, 2)
    machine.hook(0x5DE080, idle, 1)
    cases = ((0, 0, 0, 19, True), (0, 0, 0, 20, False), (0, 1, 0, 20, True),
             (0, 0, 1, 20, True), (8, 0, 0, 20, True), (9, 0, 0, 20, False))
    for command_id, group, signaled, distance, retreats in cases:
        command.command, think.distance = command_id, distance
        scripted.update(command=command_address if command_id else 0, group=group, signal=signaled)
        calls.clear()
        machine.call(0x5E7F80, think_address, ecx=game_address, edx=unit_address, limit=10000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError("retreat think did not return")
        expected = [("command", unit_address)]
        if command_id != 8:
            expected.extend((("scan", unit_address, 0, 0x5E5710), ("signal", unit_address)))
        if retreats:
            if command_id != 8:
                expected.append(("broadcast", game_address, unit_address, 8))
            expected.extend((("unlink", unit_address), ("switch", game_address, unit_address, control_address, 0)))
        expected.append(("idle", game_address, unit_address, 10))
        if calls != expected:
            raise AssertionError(f"unexpected calls: {calls!r} != {expected!r}")
    print(f"ok: {len(cases)} command-8 retreat branches")


if __name__ == "__main__":
    main()
