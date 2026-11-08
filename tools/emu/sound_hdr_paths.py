# SPDX-License-Identifier: GPL-3.0-or-later
"""Check stock SoundHdr sound-path selection and length guard.

Run: uv run python sound_hdr_paths.py

FUN_00482710 builds paths from a synthetic Sounds.txt record table; its
diagnostic boundary is hooked. It runs no filesystem or audio operations.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
BUILD_PATH = 0x482710
SIZE = 0x8E
LIMIT = 10000


class Fatal(Exception):
    """The game's path-too-long diagnostic."""


def require(condition, message):
    """Fail when stock execution or its result differs from the claim."""
    if not condition:
        raise RuntimeError(message)


def main():
    """Test speech and music range boundaries and the path-length limit."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d")
    machine = emu.Emu(str(exe))
    records = machine.alloc(SIZE * 0x125C)
    machine.w32(0x7BC9A0, records)
    machine.w32(0x7BC9A4, 0x125C)
    output = machine.alloc(128)

    def fatal(current):
        """Stop at the original fatal diagnostic before the exit call."""
        raise Fatal(current.arg(2))

    machine.hook(0x408A60, fatal, 0)
    cases = 0
    for sound in (0, 0xB75, 0xB76, 0x1230, 0x1231, 0x125A, 0x125B):
        prefix = "DATA\\LOCAL\\SFX\\" if 0xB76 <= sound <= 0x1230 else (
            "DATA\\GLOBAL\\MUSIC\\" if 0x1231 <= sound <= 0x125A else "DATA\\GLOBAL\\SFX\\")
        for length in (0, 1, 79 - len(prefix), 80 - len(prefix)):
            filename = b"a" * length
            machine.mu.mem_write(records + sound * SIZE, filename + b"\0")
            if len(prefix) + length >= 80:
                try:
                    machine.call(BUILD_PATH, ecx=sound, edx=output, limit=LIMIT)
                except Fatal as error:
                    require(error.args[0] == 0x3AB, f"sound {sound:#x}: diagnostic {error.args}")
                else:
                    raise RuntimeError(f"sound {sound:#x}: missing length diagnostic")
            else:
                machine.call(BUILD_PATH, ecx=sound, edx=output, limit=LIMIT)
                require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "instruction cap")
                require(machine.cstr(output) == prefix + filename.decode(), f"sound {sound:#x}: {machine.cstr(output)!r}")
            cases += 1
    print(f"ok: {cases} sound path and length-boundary cases")


if __name__ == "__main__":
    main()
