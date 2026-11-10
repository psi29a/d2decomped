# SPDX-License-Identifier: GPL-3.0-or-later
"""Check BNNews cache serialization and loading using stock 1.14d code.

Run: D2_MPQ_DIR=<game-data> uv run python bnnews_cache.py
Cache I/O, allocation, free and clock APIs are scripted. Input cache buffers
are synthetic and well-formed; the probe does not test archive integrity,
concurrency, allocation failure, or malformed/truncated buffer safety.
"""
import hashlib
import struct
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP

SHA = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CAP, COUNT, DATA, FIRST, NEWEST = 0x881C30, 0x881C34, 0x881C38, 0x881C40, 0x881C44
SAVE, LOAD = 0x51D370, 0x51DA30


def require(condition, message):
    """Reject unexpected stock behavior."""
    if not condition:
        raise AssertionError(message)


def run():
    """Check cache bytes, sorted and rejected records, and missing cache reset."""
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == SHA, "requires stock 1.14d game.exe")
    m = emu.Emu(str(exe))
    memory = m.alloc(8 * 0x408)
    stored = []
    frees = []
    cache = [None]

    def alloc(current):
        """Emulate the cache serialization buffer allocator."""
        return current.alloc(current.mu.reg_read(UC_X86_REG_ECX))

    def free(current):
        """Record the cache serialization buffer release."""
        frees.append(current.mu.reg_read(UC_X86_REG_ECX))
        return 0

    def storm_free(current):
        """Record the cache buffer and array releases."""
        frees.append(current.arg(0))
        return 0

    def write_cache(current):
        """Capture the serialized bytes and lifetime arguments."""
        stored.append((current.read(current.mu.reg_read(UC_X86_REG_EDX), current.arg(0)),
                       current.arg(2), current.arg(3)))
        return 7

    def read_cache(current):
        """Return one scripted cache buffer through the output argument."""
        if cache[0] is None:
            return 0
        current.w32(current.arg(0), current.put(cache[0]))
        current.w32(current.arg(1), len(cache[0]))
        return 1

    def fatal(current):
        """Expose the original fatal diagnostic line."""
        raise ValueError(current.arg(2))

    m.hook(0x408090, lambda _current: 0, 0)
    m.hook(0x408A60, fatal, 0)
    m.hook(0x40B380, alloc, 2)
    m.hook(0x40B3C0, free, 2)
    m.hook(0x412650, storm_free, 4)
    m.hook(0x519DB0, write_cache, 4)
    m.hook(0x519920, read_cache, 2)
    for address, (name, _handler) in list(m.slots.items()):
        if name == "GetSystemTime":
            m.slots[address] = (name, lambda current: (0, 1))
        elif name == "SystemTimeToFileTime":
            m.slots[address] = (name, lambda current: (1, 2))

    def reset():
        """Start a case with an existing preallocated array."""
        m.w32(CAP, 8)
        m.w32(COUNT, 0)
        m.w32(DATA, memory)
        m.w32(FIRST, 0)
        m.w32(NEWEST, 0)

    def invoke(address):
        """Run one original function to a normal return."""
        value = m.call(address, limit=200000)
        require(m.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{address:x}: did not return")
        require(m.mu.reg_read(UC_X86_REG_ESP) == emu.STACK + emu.STACK_SIZE - 0x1000, "stack cleanup")
        return value

    def record(stamp, text):
        """Encode a timestamp and NUL-terminated cache string."""
        return struct.pack("<I", stamp) + text + b"\0"

    reset()
    for i, (stamp, text) in enumerate([(10, b"old"), (20, b"a"), (30, b"bb")]):
        m.w32(memory + i * 0x408 + 4, stamp)
        m.mu.mem_write(memory + i * 0x408 + 8, text + b"\0")
    m.w32(COUNT, 3)
    m.w32(FIRST, 1)
    m.w32(NEWEST, 99)
    require(invoke(SAVE) == 7, "save did not propagate writer return")
    blob = struct.pack("<II", 99, 2) + record(20, b"a") + record(30, b"bb")
    require(stored == [(blob, 0xFFFFFFFF, 0xFFFFFFFF)], str(stored))
    require(len(frees) == 1, str(frees))

    reset()
    m.w32(COUNT, 1)
    m.mu.mem_write(memory + 8, b"x" * 1024 + b"\0")
    require(invoke(SAVE) == 0 and len(stored) == 1, "oversized text reached cache writer")
    reset()
    m.w32(COUNT, 256)
    try:
        invoke(SAVE)
    except ValueError as error:
        require(error.args == (0x13B,), str(error))
    else:
        raise AssertionError("256 unread entries did not take fatal path")

    for records, valid in [([(10, b"a"), (10, b"same"), (30, b"z")], True),
                           ([(20, b"x"), (10, b"y")], False),
                           ([(10, b"x" * 1024)], False), ([], True)]:
        reset()
        cache[0] = struct.pack("<II", 42, len(records)) + b"".join(record(*r) for r in records)
        result = invoke(LOAD)
        require(result == int(valid), f"load returned {result} for valid={valid}")
        require(m.r32(COUNT) == (len(records) if valid else 0), "load count")
        require(m.r32(NEWEST) == (42 if valid else 0), "load newest")
        require(m.r32(FIRST) == 0, "load first index")
        if valid:
            got = [(m.r32(memory + i * 0x408 + 4), m.cstr(memory + i * 0x408 + 8))
                   for i in range(len(records))]
            require(got == [(t, s.decode()) for t, s in records], str(got))
        else:
            require(m.r32(DATA) == 0 and m.r32(CAP) == 0, "failed load must clear array")
    reset()
    large = m.alloc(256 * 0x408)
    m.w32(CAP, 256)
    m.w32(DATA, large)
    records = [(i + 1, b"") for i in range(256)]
    cache[0] = struct.pack("<II", 42, 256) + b"".join(record(*r) for r in records)
    require(invoke(LOAD) == 1 and m.r32(COUNT) == 256, "256-entry cache load")
    require(m.r32(large + 255 * 0x408 + 4) == 256, "last loaded timestamp")
    m.w32(FIRST, 1)
    require(invoke(SAVE) == 7, "255 unread entries should save")
    expected = struct.pack("<II", 42, 255) + b"".join(record(*r) for r in records[1:])
    require(stored[-1] == (expected, 0xFFFFFFFF, 0xFFFFFFFF), "255-entry serialization")
    reset()
    cache[0] = struct.pack("<II", 42, 257)
    require(invoke(LOAD) == 0 and m.r32(DATA) == 0, "257 entries must be rejected")
    reset()
    cache[0] = None
    require(invoke(LOAD) == 0 and m.r32(DATA) == 0 and m.r32(CAP) == 0,
            "missing cache did not clear array")
    print("ok: news cache serialization, sorted load and rejection cases")


if __name__ == "__main__":
    run()
