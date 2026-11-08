# SPDX-License-Identifier: GPL-3.0-or-later
"""Check game.exe 1.14d's sound-record falloff range getters.

Run: uv run python sound_hdr_ranges.py

FUN_004825b0 and FUN_00482610 return x87 floats according to the dword
at Sounds.txt record +0x47. A tiny x86 thunk stores ST0 after each call.
The game code and constants execute unchanged; records are synthetic.
"""
import argparse
import hashlib
import struct
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
TABLE = 0x7BC9A0
COUNT = 0x7BC9A4
NEAR = 0x4825B0
FAR = 0x482610
LIMIT = 10000


def require(condition, message):
    """Reject mismatches and calls that did not finish."""
    if not condition:
        raise RuntimeError(message)


def main():
    """Check both getters on boundary values and distinct table rows."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d")
    machine = emu.Emu(str(exe))
    records = machine.alloc(0x8E * 3)
    machine.w32(TABLE, records)
    machine.w32(COUNT, 3)
    result = machine.alloc(4)
    thunk = machine.alloc(32)
    cases = 0
    for falloff, expected in ((0, (60., 400.)), (1, (60., 700.)), (2, (200., 1000.)),
                              (3, (400., 1500.)), (4, (2000., 2000.)), (5, (60., 700.)),
                              (0xFFFFFFFF, (60., 700.))):
        for index in range(3):
            machine.w32(records + index * 0x8E + 0x47, falloff)
            for address, want in zip((NEAR, FAR), expected, strict=True):
                code = b"\xe8" + struct.pack("<i", address - thunk - 5) + b"\xd9\x1d" + struct.pack("<I", result) + b"\xc3"
                machine.mu.mem_write(thunk, code)
                machine.mu.ctl_remove_cache(thunk, thunk + len(code))
                machine.call(thunk, ecx=index, limit=LIMIT)
                require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{address:#x}: instruction cap")
                got = struct.unpack("<f", machine.read(result, 4))[0]
                require(got == want, f"{address:#x} row {index} falloff {falloff:#x}: {got} != {want}")
                cases += 1
    print(f"ok: {cases} falloff minimum/maximum getter cases")


if __name__ == "__main__":
    main()
