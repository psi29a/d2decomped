# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005b3750's Act IV opening alert in stock game.exe 1.14d.

Run: python quest_act4_open_alert.py --exe /path/to/game.exe
The flag test is hooked; only the low return byte is meaningful.
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
    calls, scripted = [], {"result": 0}

    def test_bit(current):
        calls.append(tuple(current.arg(index) for index in range(3)))
        return scripted["result"]

    machine.hook(0x65c310, test_bit, 3)
    count = 0
    for npc in (0x16f, 0x16e, 0):
        for flag in (0, 1, 2, 0x100):
            for initial in (0, 0xabcdef12):
                scripted["result"] = flag
                calls.clear()
                result = machine.call(0x5b3750, 0x1234, flags, 0x5678, ecx=record, edx=npc,
                                      regs={"eax": initial}, limit=1000)
                expected = [(flags, 0x18, 0)] if npc == 0x16f else []
                if ((result & 0xff) != int(npc == 0x16f and flag == 0) or calls != expected or
                        machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or
                        machine.mu.reg_read(UC_X86_REG_ESP) != emu.STACK + emu.STACK_SIZE - 0x1000):
                    raise AssertionError(f"npc={npc}, flag={flag}, eax={initial}, calls={calls!r}")
                count += 1
    print(f"ok: {count} Act IV opening alert cases (flag test hooked)")


if __name__ == "__main__":
    main()
