# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005b5840's quest-object mode initialization in stock game.exe 1.14d.

Run: python quest_diablo_object_mode.py --exe /path/to/game.exe
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
    selected = {"record": 0}

    def lookup(current):
        calls.append(("lookup", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
        return selected["record"]

    def mode(current):
        calls.append(("mode", current.arg(0), current.arg(1)))
        return 0

    machine.hook(0x543640, lookup, 0)
    machine.hook(0x624690, mode, 2)
    for present in (0, 1):
        for value in (0, 1, 2, 3, 0xffffffff):
            calls.clear()
            selected["record"] = record if present else 0
            machine.w32(data + 0x40, value)
            machine.call(0x5b5840, ecx=event, limit=1000)
            expected = [("lookup", game, 0x17)] + ([("mode", unit, 2 if value == 2 else 1)] if present else [])
            expected_state = 2 if present else value
            if (calls != expected or machine.r32(data + 0x40) != expected_state or
                    machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or
                    machine.mu.reg_read(UC_X86_REG_ESP) != emu.STACK + emu.STACK_SIZE - 0x1000):
                raise AssertionError(f"present={present}, state={value}, calls={calls!r}")
    print("ok: 10 Terror's End object initialization cases (callees hooked)")


if __name__ == "__main__":
    main()
