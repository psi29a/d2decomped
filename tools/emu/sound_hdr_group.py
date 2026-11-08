# SPDX-License-Identifier: GPL-3.0-or-later
"""Check stock SoundHdr group selection (FUN_00482680).

Run: uv run python sound_hdr_group.py

Synthetic records and a scripted random-number hook check group-size,
history and retry decisions. This does not exercise game.exe's RNG itself.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CHOOSE = 0x482680
LIMIT = 10000


def require(condition, message):
    """Fail if an outcome, random bound or return boundary differs."""
    if not condition:
        raise RuntimeError(message)


def main():
    """Exercise ungrouped sounds, short-group history and retries."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d")
    machine = emu.Emu(str(exe))
    records = machine.alloc(0x8E * 8)
    machine.w32(0x7BC9A0, records)
    machine.w32(0x7BC9A4, 8)
    values, bounds = [], []

    def random(current):
        """Return scripted RNG results and record their requested bound."""
        bound = current.mu.reg_read(UC_X86_REG_ECX)
        bounds.append(bound)
        require(values, "unexpected RNG call")
        value = values.pop(0)
        require(0 <= value < bound, f"RNG value {value} outside {bound}")
        return value

    machine.hook(0x4E40A0, random, 0)
    for size, history, script, expected, expected_bounds in (
        (0, (0, 0), (), 2, ()),
        (1, (0, 0), (), 2, ()),
        (2, (2, 3), (0, 0), 2, (3, 2)),
        (2, (2, 3), (1, 0, 1), 3, (3, 2, 2)),
        (3, (2, 3), (1, 0, 1, 2), 4, (4, 3, 3, 3)),
        (4, (2, 3), (0, 1, 2), 4, (4, 4, 4)),
    ):
        row = records + 2 * 0x8E
        machine.mu.mem_write(row + 0x3D, bytes([size]))
        machine.w32(row + 0x64, history[0])
        machine.w32(row + 0x68, history[1])
        values[:] = script
        bounds.clear()
        got = machine.call(CHOOSE, ecx=2, limit=LIMIT)
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "instruction cap")
        require((got, tuple(bounds), values) == (expected, expected_bounds, []), f"size {size}: {got}, {bounds}, {values}")
    machine.w32(0x7BC9A0, 0)
    require(machine.call(CHOOSE, ecx=17, limit=LIMIT) == 17 and machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC,
            "absent table must return the input")
    print("ok: SoundHdr group selection and history retry cases")


if __name__ == "__main__":
    main()
