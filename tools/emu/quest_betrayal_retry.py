# SPDX-License-Identifier: GPL-3.0-or-later
"""Check Betrayal portal retry FUN_0058b940 in stock game.exe 1.14d.

Run: python quest_betrayal_retry.py --exe /path/to/game.exe
Quest lookup, portal attempt and event scheduling are hooked and logged.
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
    game, record, data, unit = (machine.alloc(0x200) for _ in range(4))
    machine.w32(record + 0x18, data)
    calls = []
    chosen = {"record": 0, "result": 0}

    def lookup(current):
        calls.append(("lookup", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
        return chosen["record"]

    def portal(current):
        calls.append(("portal", current.arg(0)))
        return chosen["result"]

    def event(current):
        calls.append(("event", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      *(current.arg(index) for index in range(4))))
        return 0

    machine.hook(0x543640, lookup, 0)
    machine.hook(0x58af90, portal, 1)
    machine.hook(0x5417d0, event, 4)
    count = 0
    for present in (0, 1):
        for enabled in (0, 1, 255):
            for result in (0, 1, 0x100, 0xff):
                for frame in (10, 0xfffffff8):
                    chosen.update(record=record if present else 0, result=result)
                    machine.mu.mem_write(data + 0x87, bytes([enabled]))
                    machine.w32(game + 0xa8, frame)
                    calls.clear()
                    machine.call(0x58b940, ecx=game, edx=unit, limit=1000)
                    expected = [("lookup", game, 0x22)]
                    if present and enabled:
                        expected.append(("portal", record))
                        if not (result & 0xff):
                            expected.append(("event", game, unit, 7, (frame + 12) & 0xffffffff, 0, 0))
                    if (calls != expected or machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or
                            machine.mu.reg_read(UC_X86_REG_ESP) != emu.STACK + emu.STACK_SIZE - 0x1000):
                        raise AssertionError(f"case {(present, enabled, result, frame)}: {calls!r}")
                    count += 1
    print(f"ok: {count} Betrayal portal retry cases (callees hooked)")


if __name__ == "__main__":
    main()
