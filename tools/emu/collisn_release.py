# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the room collision-grid teardown in stock game.exe 1.14d.

    uv run python collisn_release.py

FUN_0064ca10 takes the room in EDX. When it and room1 (room + 0x20) are
non-null, it calls the allocator's debug-free entry for that room1 record,
then clears the room1 pointer. Null room and null room1 are no-ops. The
allocator entry is captured so this probe never frees emulator memory.
No MPQs are read.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
FREE_ENTRY = 0x40B480
RELEASE_ROOM = 0x64CA10
FILE_NAME = 0x6EB730
SOURCE_LINE = 0x1C1
INSTRUCTION_LIMIT = 10000


def require(condition, message):
    """Stop the probe when a required stock-code observation is absent."""
    if not condition:
        raise RuntimeError(message)


def run_case(machine, room, grid, expected_free, freed):
    """Run one bounded release call and verify its pointer and allocator effects."""
    before = len(freed)
    machine.w32(room + 0x20, grid)
    machine.call(RELEASE_ROOM, ecx=0x1234, edx=room, limit=INSTRUCTION_LIMIT)
    require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC,
            "FUN_0064ca10 did not reach its return within the instruction limit")
    require(machine.r32(room + 0x20) == 0,
            "FUN_0064ca10 did not clear room + 0x20")
    if expected_free:
        require(freed[before:] == [(0x1234, grid, FILE_NAME, SOURCE_LINE, 0)],
                f"unexpected allocator free arguments: {freed[before:]!r}")
    else:
        require(len(freed) == before,
                f"unexpected allocator free for a null input: {freed[before:]!r}")
    return len(freed) - before


def main():
    """Verify both no-op paths and the room1 release path against stock code."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    digest = hashlib.sha256(exe.read_bytes()).hexdigest()
    require(digest == GAME_SHA256, "requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(exe))
    free_calls = []
    room_empty = machine.alloc(0x100)
    room_live = machine.alloc(0x100)
    grid = machine.alloc(0x80)
    freed = []

    def capture_free(current):
        """Record debug-free arguments without releasing emulator memory."""
        freed.append((current.mu.reg_read(UC_X86_REG_ECX),
                      current.mu.reg_read(UC_X86_REG_EDX),
                      current.arg(0), current.arg(1), current.arg(2)))
        return 0

    machine.hook(FREE_ENTRY, capture_free, 3)

    # Null room exits before either the getter or the allocator.
    machine.call(RELEASE_ROOM, ecx=0x1234, edx=0, limit=INSTRUCTION_LIMIT)
    require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC,
            "null-room call did not reach its return within the instruction limit")
    require(not freed, "null-room call invoked the allocator")

    # Null room1 is unchanged and does not invoke the debug-free entry.
    free_calls.append(run_case(machine, room_empty, 0, False, freed))
    # A live room1 is freed exactly once, then the room pointer is cleared.
    free_calls.append(run_case(machine, room_live, grid, True, freed))
    # Releasing that room again sees the cleared pointer and cannot double-free.
    free_calls.append(run_case(machine, room_live, 0, False, freed))
    require(free_calls == [0, 1, 0], f"unexpected case results: {free_calls!r}")
    print("ok: null room, null room1, one release, and repeat release")


if __name__ == "__main__":
    main()
