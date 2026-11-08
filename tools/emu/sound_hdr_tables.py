# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the stock SoundHdr table loader's postprocessing.

Run: uv run python sound_hdr_tables.py

FUN_00481950 builds table descriptors, allocates records, calculates group
bases, block counts and song-id bounds. Archive reading and text parsing
are hooked to supply small synthetic tables; no game assets are extracted.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
LOAD = 0x481950
LIMIT = 100000


def require(condition, message):
    """Fail if the loader changes behavior or stops before returning."""
    if not condition:
        raise RuntimeError(message)


def main():
    """Run the loader with controlled records and inspect its output."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d")
    machine = emu.Emu(str(exe))
    paths, allocations, descriptors = [], [], []
    parsed = [0]

    def archive(current):
        """Return a dummy archive and initialize the reported file size."""
        current.w32(current.arg(0), 12)
        return 0x1234

    def read_file(current):
        """Record the requested table filename and return synthetic bytes."""
        paths.append(current.cstr(current.mu.reg_read(UC_X86_REG_EDX)))
        return current.put(b"synthetic\0")

    def parser(_current):
        """Give each parsed table a distinct opaque handle."""
        parsed[0] += 1
        return parsed[0]

    def row_count(current):
        """Return six sound rows and three environment rows."""
        return {1: 6, 2: 3}[current.arg(0)]

    def allocate(current):
        """Record size and site before allocating the requested block."""
        size = current.mu.reg_read(UC_X86_REG_ECX)
        allocations.append((size, current.arg(0)))
        return current.alloc(size)

    def fill(current):
        """Check descriptor entries and supply controlled row values."""
        handle, columns, rows, count, stride = (current.arg(index) for index in range(5))
        require((count, stride) == ((6, 0x8E) if handle == 1 else (3, 0x58)), "table shape")
        descriptors.append((current.cstr(current.r32(columns)), current.r32(columns + 12)))
        if handle == 1:
            for index, group in enumerate((0, 3, 0, 0, 2, 0)):
                row = rows + index * stride
                current.mu.mem_write(row + 0x3D, bytes([group]))
                for block in range(3):
                    current.w32(row + 0x54 + block * 4, -1 if block >= index % 4 else 17)
        else:
            for index, song in enumerate((4, 2, 0)):
                current.w32(rows + index * stride, song)
        return 0

    machine.hook(0x4FAC80, archive, 3)
    machine.hook(0x517079, read_file, 3)
    machine.hook(0x6BD640, parser, 2)
    machine.hook(0x6BCDE0, row_count, 1)
    machine.hook(0x6BD780, fill, 5)
    machine.hook(0x6BCDA0, lambda _current: 0, 1)
    machine.hook(0x40B380, allocate, 2)
    machine.call(LOAD, limit=LIMIT)
    require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "loader stopped at instruction cap")
    require(paths == ["DATA\\GLOBAL\\EXCEL\\sounds.txt", "DATA\\GLOBAL\\EXCEL\\soundenviron.txt"], f"paths: {paths}")
    require(allocations == [(6 * 0x8E, 0x106), (3 * 0x58, 0x112), (12, 0x147)], f"allocations: {allocations}")
    require(descriptors == [("Filename", 0), ("Song", 0)], f"first descriptors: {descriptors}")
    rows = machine.r32(0x7BC9A0)
    require([machine.r32(rows + index * 0x8E + 0x60) for index in range(6)] == [0, 1, 1, 1, 4, 4], "group bases")
    require([machine.read(rows + index * 0x8E + 0x3D, 1)[0] for index in range(6)] == [0, 3, 1, 1, 2, 1], "group sizes")
    require([machine.read(rows + index * 0x8E + 0x80, 1)[0] for index in range(1, 6)] == [1, 2, 3, 0, 1], "block counts")
    require((machine.r32(0x7BC9B0), machine.r32(0x7BC9B4)) == (2, 4), "song bounds")
    print("ok: synthetic SoundHdr table descriptors, group bases, block counts and song range")


if __name__ == "__main__":
    main()
