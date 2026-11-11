# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005bbb70's quest-object mode gate in stock game.exe 1.14d.

Run: python quest_temple_object.py --exe /path/to/game.exe
Quest lookup and object-mode setter are hooked; their internals are not tested.
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
    event, record, data, game, unit = (machine.alloc(0x100) for _ in range(5))
    machine.w32(event, game)
    machine.w32(event + 4, unit)
    machine.w32(record + 0x18, data)
    calls = []

    def lookup(current):
        calls.append(("lookup", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
        return record

    def mode(current):
        calls.append(("mode", current.arg(0), current.arg(1)))
        return 0

    machine.hook(0x543640, lookup, 0)
    machine.hook(0x624690, mode, 2)
    for value in (0, 1, 2, 128, 255):
        calls.clear()
        machine.mu.mem_write(data + 0xc, bytes([value]))
        machine.call(0x5bbb70, ecx=event, limit=1000)
        expected = [("lookup", game, 0x13)] + ([("mode", unit, 2)] if value else [])
        if (calls != expected or machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or
                machine.mu.reg_read(UC_X86_REG_ESP) != emu.STACK + emu.STACK_SIZE - 0x1000):
            raise AssertionError(f"flag={value}, calls={calls!r}")
    print("ok: 5 Blackened Temple object-mode cases (callees hooked)")


if __name__ == "__main__":
    main()
