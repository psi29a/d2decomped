# SPDX-License-Identifier: GPL-3.0-or-later
"""Check Hellforge alert FUN_005b5ee0 in stock game.exe 1.14d.

Run: python quest_hellforge_alert.py --exe /path/to/game.exe
Flag tests are hooked; only the low return byte and caller gates are checked.
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
    record, data, flags = (machine.alloc(0x100) for _ in range(3))
    machine.w32(record + 0x18, data)
    calls, scripted = [], [0, 0]

    def test_bit(current):
        calls.append(tuple(current.arg(index) for index in range(3)))
        return scripted[current.arg(2)]

    machine.hook(0x65c310, test_bit, 3)
    count = 0
    for npc in (0xf6, 0xf7):
        for done in (0, 1, 0x100):
            for due in (0, 1, 0x100):
                for active in (0, 1, 255):
                    for state in (0, 1, 2):
                        for given in (0, 1, 255):
                            scripted[:] = [done, due]
                            machine.mu.mem_write(record + 9, bytes([active]))
                            machine.mu.mem_write(record + 0xc, bytes([state]))
                            machine.mu.mem_write(data + 0x1c, bytes([given]))
                            calls.clear()
                            value = machine.call(0x5b5ee0, 0x1234, flags, 0x5678, ecx=record, edx=npc,
                                                 regs={"eax": 0xabcdefff}, limit=1000)
                            expected = []
                            if npc == 0xf6:
                                expected.append((flags, 0x1b, 0))
                                if done == 0:
                                    expected.append((flags, 0x1b, 1))
                            allowed = npc == 0xf6 and done == 0 and due == 0 and active != 0 and state == 1 and given == 0
                            if ((value & 0xff) != int(allowed) or calls != expected or
                                    machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or
                                    machine.mu.reg_read(UC_X86_REG_ESP) != emu.STACK + emu.STACK_SIZE - 0x1000):
                                raise AssertionError(f"case {(npc, done, due, active, state, given)}: {calls!r}")
                            count += 1
    print(f"ok: {count} Hellforge alert cases (flag test hooked)")


if __name__ == "__main__":
    main()
