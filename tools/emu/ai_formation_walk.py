# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005e5870's group, follower and leader formation decisions.

    uv run python ai_formation_walk.py --exe /path/to/game.exe

Commands, group search, owner, coordinates, random helpers and actions are
scripted. The leader's initial unit-seed draw runs for real; no MPQs are read.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    game_address, unit_address, owner_address = (machine.alloc(0x200) for _ in range(3))
    think_address, control_address, command_address = (machine.alloc(0x40) for _ in range(3))
    unit, control = machine.view("unit", unit_address), machine.view("ai_control", control_address)
    command = machine.view("ai_command", command_address)
    machine.view("ai_think", think_address).control = control_address
    command.x, command.y = 200, 300
    calls = []
    scripted = {"command": 0, "group": 0, "signal": 0, "owner": owner_address, "valid": 0, "coin": 0}

    def current_command(current):
        calls.append(("command", current.mu.reg_read(UC_X86_REG_ECX)))
        return scripted["command"]

    def scan(current):
        calls.append(("scan", current.mu.reg_read(UC_X86_REG_ECX), current.arg(0), current.arg(1)))
        current.w32(current.mu.reg_read(UC_X86_REG_EDX), scripted["group"])
        return 0

    def signal(current):
        calls.append(("signal", current.mu.reg_read(UC_X86_REG_ECX)))
        return scripted["signal"]

    def owner(current):
        calls.append(("owner", current.mu.reg_read(UC_X86_REG_ECX)))
        return scripted["owner"]

    def coordinate(name, value):
        def callback(current):
            calls.append((name, current.mu.reg_read(UC_X86_REG_ECX)))
            return value
        return callback

    def roll(current):
        calls.append(("roll", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
        return 3

    def coin(current):
        calls.append(("coin", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
        return scripted["coin"]

    def valid(current):
        calls.append(("valid", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      current.arg(0), current.arg(1)))
        return scripted["valid"]

    def broadcast(current):
        packet = machine.view("ai_command", current.arg(0))
        values = (packet.command,) if packet.command == 8 else (packet.command, packet.x, packet.y)
        calls.append(("broadcast", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX), values))
        return 0

    def simple(name, count):
        def callback(current):
            calls.append((name, current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                          tuple(current.arg(i) for i in range(count))))
            return 0
        return callback

    def unlink(current):
        calls.append(("unlink", current.mu.reg_read(UC_X86_REG_ECX)))
        return 0

    machine.hook(0x58EE80, current_command, 0)
    machine.hook(0x58F380, scan, 2)
    machine.hook(0x5DD2B0, signal, 0)
    machine.hook(0x58F0D0, owner, 0)
    machine.hook(0x45ADF0, coordinate("x", 100), 0)
    machine.hook(0x45AE20, coordinate("y", 90), 0)
    machine.hook(0x45C390, roll, 0)
    machine.hook(0x472210, coin, 0)
    machine.hook(0x5DEE50, valid, 2)
    machine.hook(0x58F730, broadcast, 1)
    machine.hook(0x58F160, unlink, 0)
    machine.hook(0x58ED10, simple("consume", 0), 0)
    machine.hook(0x5DED90, simple("move", 2), 2)
    machine.hook(0x5B0E00, simple("switch", 2), 2)
    machine.hook(0x5DE080, simple("idle", 1), 1)
    cases = (("command8", 8, 0, 0, False, 0, 0, 1),
             ("group", 0, 1, 0, False, 0, 0, 1),
             ("signal", 0, 0, 1, False, 0, 0, 1),
             ("follower-idle", 0, 0, 0, False, 0, 0, 1),
             ("follower-move", 2, 0, 0, False, 0, 0, 1),
             ("leader-idle", 0, 0, 0, True, 0, 0, 3),
             ("north", 0, 0, 0, True, 0, 1, 2),
             ("east", 0, 0, 0, True, 24, 1, 2),
             ("south", 0, 0, 0, True, 49, 1, 2),
             ("west", 0, 0, 0, True, 74, 1, 2),
             ("invalid", 0, 0, 0, True, 99, 0, 2))
    for label, command_id, group, signal_value, leader, phase, valid_value, seed in cases:
        command.command = command_id
        control.scratch_one, control.scratch_two, control.scratch_three = 4, 7, phase
        unit.seed_low, unit.seed_high = seed, 666
        scripted.update(command=command_address if command_id else 0, group=group, signal=signal_value,
                        owner=unit_address if leader else owner_address, valid=valid_value, coin=0)
        calls.clear()
        machine.call(0x5E5870, think_address, ecx=game_address, edx=unit_address, limit=10000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError(f"{label}: did not return")
        expected = [("command", unit_address)]
        if command_id != 8:
            expected.extend((("scan", unit_address, 0, 0x5E5710), ("signal", unit_address)))
        if label in ("group", "signal"):
            expected.append(("broadcast", game_address, unit_address, (8,)))
        if label in ("group", "signal", "command8"):
            expected.extend((("unlink", unit_address), ("switch", game_address, unit_address, (control_address, 0)),
                             ("idle", game_address, unit_address, (10,))))
        else:
            expected.append(("owner", unit_address))
            if label == "follower-idle":
                expected.append(("idle", game_address, unit_address, (15,)))
            elif label == "follower-move":
                expected.extend((("consume", game_address, unit_address, ()),
                                 ("move", game_address, unit_address, (204, 307))))
            else:
                stepped = seed * 0x6AC690C5 + 666
                if (unit.seed_low, unit.seed_high) != (stepped & 0xFFFFFFFF, stepped >> 32):
                    raise AssertionError(f"{label}: seed differs")
                if (stepped & 0xFFFFFFFF) % 100 < 30:
                    expected.append(("idle", game_address, unit_address, (10,)))
                else:
                    new_phase = (phase + 1) if phase + 1 < 100 else 0
                    x, y = ((103, 85) if new_phase < 25 else (105, 93) if new_phase < 50 else
                            (103, 95) if new_phase < 75 else (95, 93))
                    expected.extend((("x", unit_address), ("y", unit_address), ("roll", unit_address + 0x20, 10),
                                     ("coin", unit_address + 0x20, 2), ("valid", game_address, unit_address, x, y)))
                    if valid_value:
                        expected.append(("broadcast", game_address, unit_address, (2, x, y)))
                    if control.scratch_three != new_phase + (0 if valid_value else 25):
                        raise AssertionError(f"{label}: phase differs")
        if calls != expected:
            raise AssertionError(f"{label}: {calls!r} != {expected!r}")
    print(f"ok: {len(cases)} formation walk decision cases")


if __name__ == "__main__":
    main()
