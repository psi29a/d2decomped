# SPDX-License-Identifier: GPL-3.0-or-later
"""Check Cain's item count callback FUN_00592b90 in game.exe 1.14d.

Run: python quest_cain_scroll_count.py --exe /path/to/game.exe
The item lookup is hooked; this checks the callback, not lookup behavior.
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
    record, data, event, game, player = (machine.alloc(0x100) for _ in range(5))
    machine.w32(record + 0x18, data)
    machine.w32(event, game)
    machine.w32(event + 0xc, player)
    calls = []
    present = set()

    def lookup(current):
        calls.append((current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX), current.arg(0)))
        return int(current.arg(0) in present)

    machine.hook(0x558110, lookup, 1)
    for initial in (0, 1, 0xfffffffe, 0xffffffff):
        for mask in range(4):
            present.clear()
            present.update(code for index, code in enumerate((0x20646b62, 0x20736b62)) if mask & (1 << index))
            calls.clear()
            machine.w32(data + 0x7c, initial)
            machine.call(0x592b90, ecx=record, edx=event, limit=1000)
            expected = [(game, player, code) for code in (0x20646b62, 0x20736b62)]
            if (calls != expected or machine.r32(data + 0x7c) != (initial + len(present)) & 0xffffffff or
                    machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or
                    machine.mu.reg_read(UC_X86_REG_ESP) != emu.STACK + emu.STACK_SIZE - 0x1000):
                raise AssertionError(f"count={initial}, mask={mask}, calls={calls!r}")
    print("ok: 16 Cain scroll-count cases (lookup hooked)")


if __name__ == "__main__":
    main()
