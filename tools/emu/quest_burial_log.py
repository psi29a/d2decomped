# SPDX-License-Identifier: GPL-3.0-or-later
"""Check Blood Raven's log-send filter, FUN_00590830, in game.exe 1.14d.

Run: python quest_burial_log.py --exe /path/to/game.exe
The player-data lookup, bit test and packet sender are hooked. This checks
filter decisions and helper arguments, not the helpers or network behavior.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    game, player, player_data = (machine.alloc(0x100) for _ in range(3))
    buffers = [machine.alloc(0x60) for _ in range(3)]
    for index, pointer in enumerate(buffers):
        machine.w32(player_data + 0x10 + 4 * index, pointer)
    calls = []
    bits = set()

    def data_lookup(current):
        calls.append(("data", current.arg(0)))
        return player_data

    def test_bit(current):
        calls.append(("bit", current.arg(0), current.arg(1), current.arg(2)))
        return int(current.arg(2) in bits)

    def send(current):
        calls.append(("send", current.mu.reg_read(UC_X86_REG_ECX),
                      current.mu.reg_read(UC_X86_REG_EDX), current.arg(0)))
        return 0

    machine.hook(0x6221a0, data_lookup, 1)
    machine.hook(0x65c310, test_bit, 3)
    machine.hook(0x544190, send, 1)
    for difficulty in range(3):
        machine.mu.mem_write(game + 0x6d, bytes([difficulty]))
        for mask in range(16):
            bits.clear()
            bits.update(bit for index, bit in enumerate((0, 15, 13, 14)) if mask & (1 << index))
            calls.clear()
            result = machine.call(0x590830, 0xabcdef, ecx=game, edx=player, limit=1000)
            tested = [0]
            if 0 not in bits:
                tested.append(15)
            if 0 in bits or 15 in bits:
                tested.append(13)
                if 13 not in bits:
                    tested.append(14)
            expected = [("data", player)] + [("bit", buffers[difficulty], 2, bit) for bit in tested]
            if not ((0 in bits or 15 in bits) and 13 not in bits and 14 not in bits):
                expected.append(("send", game, player, 2))
            if (result != 0 or calls != expected or machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or
                    machine.mu.reg_read(UC_X86_REG_ESP) != emu.STACK + emu.STACK_SIZE - 0x1000):
                raise AssertionError(f"difficulty={difficulty} bits={bits}: {calls!r}, result={result}")
    print("ok: 48 Blood Raven log-filter cases (callees hooked)")


if __name__ == "__main__":
    main()
