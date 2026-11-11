# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005de330's chatter and packet decisions in stock 1.14d.

    uv run python ai_dialogue.py --exe /path/to/game.exe

Formatting, chat allocation, sound and packet helpers are scripted; no MPQs
are read and no network operation is performed.
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
    game_address, unit_address, pool = machine.alloc(0x200), machine.alloc(0x100), machine.alloc(0x20)
    node_address = machine.alloc(0x20)
    game, unit = machine.view("game", game_address), machine.view("unit", unit_address)
    game.pool, game.frame = pool, 123
    unit.id = 456
    calls = []
    scripted = {"node": 0}

    def formatter(current):
        text = str(current.arg(2)).encode("ascii") + b"\0"
        current.mu.mem_write(current.arg(0), text)
        calls.append(("format", current.cstr(current.arg(1)), current.arg(2)))
        return len(text) - 1

    def free(current):
        calls.append(("free", current.arg(0), current.arg(1)))
        return 0

    def create(current):
        calls.append(("create", current.arg(0), current.cstr(current.arg(1)), current.arg(2)))
        return scripted["node"]

    def notify(current):
        calls.append(("notify", current.arg(0)))
        return 0

    def timestamp(current):
        calls.append(("time", current.arg(0)))
        return 0xABCD

    def sound(current):
        calls.append(("sound", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      tuple(current.arg(i) for i in range(4))))
        return 0

    def client(current):
        calls.append(("client", current.mu.reg_read(UC_X86_REG_ECX), current.arg(0)))
        return 0x1234

    def packet(current):
        address = current.mu.reg_read(UC_X86_REG_EDX)
        calls.append(("packet", current.mu.reg_read(UC_X86_REG_ECX), current.read(address, 12)))
        return 0

    machine.hook(0x6823FD, formatter, 0)
    machine.hook(0x6611A0, free, 2)
    machine.hook(0x661110, create, 3)
    machine.hook(0x64C040, notify, 1)
    machine.hook(0x6611D0, timestamp, 1)
    machine.hook(0x5417D0, sound, 4)
    machine.hook(0x5531C0, client, 1)
    machine.hook(0x53C8D0, packet, 0)
    cases = ((0, False, unit_address, False), (65536, False, unit_address, False),
             (23, False, unit_address, False), (23, False, unit_address, True),
             (23, False, 0, True), (23, True, unit_address, False), (23, True, 0, False))
    for line, networked, unit_arg, succeeds in cases:
        unit.chat_node, unit.flags = 0x9988, 0x20
        scripted["node"] = node_address if succeeds else 0
        calls.clear()
        machine.call(0x5DE330, 0x4444, line, int(networked), ecx=game_address, edx=unit_arg, limit=10000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError("dialogue did not return")
        if line & 0xFFFF == 0:
            expected = []
        elif networked:
            packet_bytes = bytes((0x27, 1)) + (456 if unit_arg else 0xFFFFFFFF).to_bytes(4, "little")
            packet_bytes += bytes((1, 0, 3, 0)) + (line & 0xFFFF).to_bytes(2, "little")
            expected = [("client", 0x4444, 0x2CB), ("packet", 0x1234, packet_bytes)]
        else:
            expected = [("format", "%d", line & 0xFFFF)]
            if unit_arg:
                expected.append(("free", pool, 0x9988))
            expected.append(("create", pool, str(line & 0xFFFF), 123))
            if succeeds:
                expected.extend((("notify", unit_arg), ("time", node_address),
                                 ("sound", game_address, unit_arg, (6, 0xABCD, 0, 0))))
        if calls != expected:
            raise AssertionError(f"dialogue calls differ: {calls!r} != {expected!r}")
        expected_node = node_address if succeeds and unit_arg and not networked and line & 0xFFFF else 0x9988
        expected_flags = 0x120 if succeeds and unit_arg and not networked and line & 0xFFFF else 0x20
        if (unit.chat_node, unit.flags) != (expected_node, expected_flags):
            raise AssertionError("dialogue unit state differs")
    print(f"ok: {len(cases)} dialogue and packet branches")


if __name__ == "__main__":
    main()
