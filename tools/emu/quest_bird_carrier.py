# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify Golden Bird carrier release (FUN_005bacf0) in stock 1.14d.

Run: python quest_bird_carrier.py --exe /path/to/game.exe
Synthetic records cover the activation byte, null unit and matching ID.
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
    record, data, unit = (machine.alloc(0x100) for _ in range(3))
    machine.w32(record + 0x18, data)
    count = 0
    for flag in (0, 1, 2, 255):
        for identifier in (0, 9, 0xffffffff):
            for unit_id in (None, 0, 9, 0xffffffff):
                machine.mu.mem_write(data + 1, bytes((0x55, flag)))
                machine.w32(data + 4, identifier)
                if unit_id is not None:
                    machine.w32(unit + 0xc, unit_id)
                machine.call(0x5bacf0, ecx=record, edx=unit if unit_id is not None else 0, limit=1000)
                match = flag == 1 and identifier == (0xffffffff if unit_id is None else unit_id)
                expected = bytes((1, 0)) if match else bytes((0x55, flag))
                if (machine.read(data + 1, 2) != expected or machine.r32(data + 4) != identifier or
                        machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or
                        machine.mu.reg_read(UC_X86_REG_ESP) != emu.STACK + emu.STACK_SIZE - 0x1000):
                    raise AssertionError(f"flag={flag}, stored={identifier:x}, unit={unit_id}")
                count += 1
    print(f"ok: {count} Golden Bird carrier release cases")


if __name__ == "__main__":
    main()
