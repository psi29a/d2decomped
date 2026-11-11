# SPDX-License-Identifier: GPL-3.0-or-later
"""Check Prison of Ice position gate FUN_0058a7d0 in stock game.exe 1.14d.

Run: python quest_prison_position.py --exe /path/to/game.exe
Quest lookup is hooked; this checks the gate and copied coordinates.
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
    game, record, data, output = (machine.alloc(0x100) for _ in range(4))
    machine.w32(record + 0x18, data)
    machine.w32(data + 0xb8, 0x12345678)
    machine.w32(data + 0xbc, 0xabcdef01)
    calls = []
    selected = {"record": 0}

    def lookup(current):
        calls.append((current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
        return selected["record"]

    machine.hook(0x543640, lookup, 0)
    count = 0
    for present in (0, 1):
        for active in (0, 1, 255):
            for ready in (0, 1, 255):
                for blocked in (0, 1, 255):
                    selected["record"] = record if present else 0
                    machine.mu.mem_write(record + 9, bytes([active]))
                    machine.mu.mem_write(data + 0xb2, bytes([ready]))
                    machine.mu.mem_write(data + 0xae, bytes([blocked]))
                    machine.w32(output, 0xdeadbeef)
                    machine.w32(output + 4, 0xfedcba98)
                    calls.clear()
                    result = machine.call(0x58a7d0, output, ecx=game, limit=1000)
                    allowed = bool(present and active and ready and not blocked)
                    expected = (0x12345678, 0xabcdef01) if allowed else (0xdeadbeef, 0xfedcba98)
                    if (result != int(allowed) or (machine.r32(output), machine.r32(output + 4)) != expected or
                            calls != [(game, 0x21)] or machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or
                            machine.mu.reg_read(UC_X86_REG_ESP) != emu.STACK + emu.STACK_SIZE - 0x1000):
                        raise AssertionError(f"gate mismatch: {(present, active, ready, blocked)}")
                    count += 1
    print(f"ok: {count} Prison of Ice position cases (lookup hooked)")


if __name__ == "__main__":
    main()
