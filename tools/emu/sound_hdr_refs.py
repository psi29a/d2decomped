# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise stock SoundHdr group reference counts (game.exe 1.14d).

Run: uv run python sound_hdr_refs.py

FUN_004822b0 adjusts each group member's count; FUN_00482370 clears it.
Uses synthetic sound records. The original diagnostic boundary is hooked.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
ADJUST = 0x4822B0
CLEAR = 0x482370
TABLE = 0x7BC9A0
COUNT = 0x7BC9A4
SIZE = 0x8E
LIMIT = 10000


class Fatal(Exception):
    """The diagnostic call indicating a group count underflow."""


def require(condition, message):
    """Reject mismatches and instruction-limit stops."""
    if not condition:
        raise RuntimeError(message)


def main():
    """Check group increments, decrements, clearing and invalid outer IDs."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d")
    machine = emu.Emu(str(exe))
    records = machine.alloc(SIZE * 6)
    machine.w32(TABLE, records)
    machine.w32(COUNT, 6)

    def fatal(current):
        """Stop when the game's unrecoverable diagnostic is reached."""
        raise Fatal(current.arg(2))

    machine.hook(0x408A60, fatal, 0)

    def call(address, sound, delta=0):
        """Run the chosen routine to its return, with an optional delta."""
        machine.call(address, ecx=sound, edx=delta, limit=LIMIT)
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{address:#x}: instruction cap")

    def counts():
        """Read every record's current reference count."""
        return [machine.r32(records + index * SIZE + 0x8A) for index in range(6)]

    def init(values, first=1, length=3):
        """Initialize counts and select the group on record two."""
        for index, value in enumerate(values):
            machine.w32(records + index * SIZE + 0x8A, value)
        machine.w32(records + 2 * SIZE + 0x60, first)
        machine.mu.mem_write(records + 2 * SIZE + 0x3D, bytes([length]))

    init([8, 1, 1, 1, 7, 6])
    call(ADJUST, 2, 1)
    require(counts() == [8, 2, 2, 2, 7, 6], f"increment: {counts()}")
    call(ADJUST, 2, 0xFFFFFFFF)
    require(counts() == [8, 1, 1, 1, 7, 6], f"decrement: {counts()}")
    call(CLEAR, 2)
    require(counts() == [8, 0, 0, 0, 7, 6], f"clear: {counts()}")
    call(ADJUST, 2, 0xFFFFFFFF)
    require(counts() == [8, 0, 0, 0, 7, 6], "zero selected count must skip decrement")
    init([8, 1, 2, 0, 7, 6])
    try:
        call(ADJUST, 2, 0xFFFFFFFF)
    except Fatal as error:
        require(error.args[0] == 0x1DC, f"wrong underflow diagnostic: {error.args}")
        require(counts() == [8, 0, 1, 0, 7, 6], f"underflow order: {counts()}")
    else:
        raise RuntimeError("missing underflow diagnostic")
    for address in (ADJUST, CLEAR):
        for sound in (0, 6, 0xFFFFFFFF):
            before = counts()
            call(address, sound, 1)
            require(counts() == before, f"{address:#x} invalid sound {sound:#x} changed counts")
    print("ok: grouped count adjustment, clearing, guards and underflow ordering")


if __name__ == "__main__":
    main()
