# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the BNNews array helpers from stock game.exe 1.14d.

Run: D2_MPQ_DIR=<game-data> uv run python bnnews_array.py
Storm realloc is forced to fail; fresh allocation and free are scripted. This
checks fallback copy, capacity/count changes and growth arithmetic, not real
heap behavior, allocation failure or out-of-range arithmetic.
"""
import hashlib
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

SHA = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
GROW, RESIZE, CLEAR, RESERVE, APPEND, EXACT, SET = (
    0x51D2F0, 0x51D5D0, 0x51D6A0, 0x51D6D0, 0x51D7A0, 0x51D7F0, 0x51D810)


def require(condition, message):
    """Reject unexpected stock behavior."""
    if not condition:
        raise AssertionError(message)


def run():
    """Check growth choice, resize fallback, reserve, append and clear."""
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == SHA, "requires stock 1.14d game.exe")
    m = emu.Emu(str(exe))
    header = m.alloc(16)
    freed = []
    sizes = []

    def allocate(current):
        """Return a fresh Storm block and record its requested size."""
        sizes.append(current.arg(0))
        return current.alloc(current.arg(0))

    def free(current):
        """Record a Storm free."""
        freed.append(current.arg(0))
        return 0

    m.hook(0x4133D0, lambda _current: 0, 5)
    m.hook(0x413020, allocate, 4)
    m.hook(0x412650, free, 4)

    def call(address, *args):
        """Call one thiscall or fastcall array routine to a normal return."""
        result = m.call(address, *args, ecx=header, limit=200000)
        require(m.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{address:x}: did not return")
        require(m.mu.reg_read(UC_X86_REG_ESP) == emu.STACK + emu.STACK_SIZE - 0x1000, "stack cleanup")
        return result

    for n, expected in [(0, 1), (1, 1), (2, 2), (3, 2), (4, 4), (7, 4), (8, 8), (20, 8)]:
        m.w32(header + 12, 0)
        require(call(GROW, n) == expected, f"growth step for {n}")
        require(m.r32(header + 12) == (8 if n >= 8 else 0), f"stored step for {n}")
    m.w32(header + 12, 0)
    call(SET, 3)
    require((m.r32(header), m.r32(header + 4)) == (4, 3), "set count growth")
    ptr = m.r32(header + 8)
    m.mu.mem_write(ptr, b"a" * 0x408 + b"b" * 0x408 + b"c" * 0x408)
    call(RESERVE, 4, 1)
    grown = m.r32(header + 8)
    require((m.r32(header), m.r32(header + 4)) == (8, 3), "rounded reserve")
    require(m.read(grown, 3 * 0x408) == b"a" * 0x408 + b"b" * 0x408 + b"c" * 0x408,
            "fallback resize copy")
    require(ptr in freed, "fallback did not free prior block")
    require(call(APPEND) == grown + 3 * 0x408 and m.r32(header + 4) == 4, "append")
    call(EXACT, 5)
    require(m.r32(header) == 9 and m.r32(header + 4) == 4, "exact reserve")
    call(SET, 2)
    require(m.r32(header) == 9 and m.r32(header + 4) == 2, "set count shrink")
    final = m.r32(header + 8)
    call(CLEAR)
    require([m.r32(header + i) for i in (0, 4, 8)] == [0, 0, 0], "clear")
    require(final in freed and sizes == [4 * 0x408, 8 * 0x408, 9 * 0x408], "allocation/free sequence")
    # Deleting destructor frees the payload, and frees the header only for bit 0.
    for flags in (0, 1):
        m.w32(header + 8, final)
        before = len(freed)
        require(call(0x51D590, flags) == header, "destructor return")
        require(freed[before:] == ([final, header] if flags else [final]), "destructor free sequence")
    print("ok: news array growth, copy fallback, reserve, append, clear and destructor")


if __name__ == "__main__":
    run()
