# SPDX-License-Identifier: GPL-3.0-or-later
"""Check The Guardian alert gate FUN_005bbfe0 in stock game.exe 1.14d.

Run: python quest_guardian_alert.py --exe /path/to/game.exe
The quest flag test is hooked; the caller's branch and helper arguments are checked.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    record, flags = (machine.alloc(0x100) for _ in range(2))
    calls, bits = [], set()

    def test_bit(current):
        calls.append((current.arg(0), current.arg(1), current.arg(2)))
        return int(current.arg(2) in bits)

    machine.hook(0x65c310, test_bit, 3)
    count = 0
    for state in (0, 1, 2, 255):
        for npc in (0xff, 0x100):
            for mask in range(4):
                machine.mu.mem_write(record + 0xc, bytes([state]))
                bits.clear()
                bits.update(bit for index, bit in enumerate((0, 11)) if mask & (1 << index))
                calls.clear()
                value = machine.call(0x5bbfe0, 0x1234, flags, 0x5678, ecx=record, edx=npc, limit=1000)
                early = 0 not in bits and npc == 0xff and state == 1
                expected = [(flags, 0x16, 0)] + ([] if early else [(flags, 0x16, 11)])
                if ((value & 0xff) != int(early or 11 in bits) or calls != expected or
                        machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or
                        machine.mu.reg_read(UC_X86_REG_ESP) != emu.STACK + emu.STACK_SIZE - 0x1000):
                    raise AssertionError(f"state={state}, npc={npc}, bits={bits}, calls={calls!r}")
                count += 1
    print(f"ok: {count} Guardian alert cases (flag test hooked)")


if __name__ == "__main__":
    main()
