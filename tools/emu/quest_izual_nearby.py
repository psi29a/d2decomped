# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005b43f0's Izual quest search gate in stock game.exe 1.14d.

Run: python quest_izual_nearby.py --exe /path/to/game.exe
Quest lookup and player iteration are hooked; the iterator is not tested.
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
    game, record, data, unit = (machine.alloc(0x100) for _ in range(4))
    machine.w32(record + 0x18, data)
    calls = []
    selected = {"record": 0, "result": 0}

    def lookup(current):
        calls.append(("lookup", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
        return selected["record"]

    def iterate(current):
        pointer = current.arg(0)
        calls.append(("iterate", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      pointer, current.r32(pointer + 0x14), current.read(pointer + 0x18, 1)[0]))
        current.mu.mem_write(pointer + 0x18, bytes([selected["result"]]))
        return 0

    machine.hook(0x543640, lookup, 0)
    machine.hook(0x5538d0, iterate, 1)
    count = 0
    for present in (0, 1):
        for active in (0, 1, 255):
            for result in (0, 1, 255):
                selected.update(record=record if present else 0, result=result)
                machine.mu.mem_write(data + 0x12, bytes([active]))
                machine.mu.mem_write(data + 0x18, b'\x73')
                machine.w32(data + 0x14, 0xdeadbeef)
                calls.clear()
                actual = machine.call(0x5b43f0, ecx=game, edx=unit, limit=1000)
                enabled = present and active
                expected = [("lookup", game, 0x16)]
                if enabled:
                    expected.append(("iterate", game, 0x5b43d0, data, unit, 0))
                if (actual != int(bool(enabled) and result == 0) or calls != expected or
                        machine.r32(data + 0x14) != (unit if enabled else 0xdeadbeef) or
                        machine.read(data + 0x18, 1)[0] != (result if enabled else 0x73) or
                        machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or
                        machine.mu.reg_read(UC_X86_REG_ESP) != emu.STACK + emu.STACK_SIZE - 0x1000):
                    raise AssertionError(f"case {(present, active, result)}: {calls!r}, {actual}")
                count += 1
    print(f"ok: {count} Izual search gate cases (callees hooked)")


if __name__ == "__main__":
    main()
