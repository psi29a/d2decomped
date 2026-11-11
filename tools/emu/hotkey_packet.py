# SPDX-License-Identifier: GPL-3.0-or-later
"""Check hotkey packet fields with game.exe 1.14d's real FUN_0054c870.

Only skill lookup and the final metadata setter are stubbed; the real handler
validates and decodes the packet. Does not connect to a host or read MPQs.
"""
import argparse
import json
import struct

from unicorn.x86_const import UC_X86_REG_EIP

import emu


def capture(executable):
    """Run the retail hotkey handler and verify its decoded metadata setter calls.

    Return wire hex and decoded fields without connecting to a host.
    """
    machine = emu.Emu(executable)
    data = machine.alloc(0xc00)
    machine.w32(0x744304, data)
    machine.w32(data + 0xba0, 400)
    calls = []
    machine.hook(0x6439b0, lambda current: 1, 1)

    def apply(current):
        """Record the four metadata-setter arguments produced by the real hotkey handler."""
        calls.append(tuple(current.arg(index) for index in range(4)))
        return 1

    machine.hook(0x5356b0, apply, 4)
    results = []
    for slot, skill, left, item in ((0, 36, 0, 0xffffffff), (7, 54, 1, 0xffffffff), (15, 39, 0, 0x12345678), (3, 0x7fff, 1, 0xffffffff)):
        packet = bytes([0x51]) + struct.pack("<II", (slot << 16) | (left << 15) | skill, item)
        pointer = machine.put(packet)
        calls.clear()
        machine.call(0x54c870, pointer, len(packet), ecx=0x12340000, edx=0x12350000, limit=10000)
        assert machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC
        expected = (slot, skill if skill < 400 else 0xffffffff, left, item)
        assert calls == [expected], (packet.hex(), calls, expected)
        results.append((packet.hex(), *calls[0]))
    return results


def main():
    """Read the local executable path and emit decoded cases or wire-only comparison rows."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable")
    parser.add_argument("--wire", action="store_true")
    args = parser.parse_args()
    results = capture(args.executable)
    if args.wire:
        for packet, *_fields in results:
            print(packet)
    else:
        print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
