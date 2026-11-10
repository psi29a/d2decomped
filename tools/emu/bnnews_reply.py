# SPDX-License-Identifier: GPL-3.0-or-later
"""Run stock BNNews.cpp's reply merge and callbacks on synthetic packets.

Run: D2_MPQ_DIR=<game-data> uv run python bnnews_reply.py
The cache writer is hooked; the preallocated array avoids allocator calls.
This does not test cache persistence, allocation failure, malformed packets,
concurrency or networking. No game process or MPQs are started.
"""
import hashlib
import struct
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

SHA = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CAP, COUNT, DATA, FIRST, NEWEST = 0x881C30, 0x881C34, 0x881C38, 0x881C40, 0x881C44
ITEM_CB, HEADER_CB = 0x881C28, 0x881C2C
REPLY, SAVE, NOTIFY = 0x51D860, 0x51D370, 0x51D510
ITEM_HOOK, HEADER_HOOK = 0x51D590, 0x51D6A0


def require(condition, message):
    """Fail explicitly if a stock observation disagrees with a case."""
    if not condition:
        raise AssertionError(message)


def run():
    """Check ordering, replacement, cutoff, callbacks and signed count guard."""
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == SHA, "requires stock 1.14d game.exe")
    m = emu.Emu(str(exe))
    entries = m.alloc(16 * 0x408)
    m.w32(CAP, 16)
    m.w32(DATA, entries)
    events = []
    save_result = [1]

    def save(current):
        """Record the state at the cache-write boundary without writing a cache."""
        events.append(("save", current.r32(FIRST), current.r32(NEWEST)))
        return save_result[0]

    def header(_current):
        """Record the header callback."""
        events.append(("header",))
        return 0

    def item(current):
        """Record the callback's timestamp and NUL-terminated text."""
        timestamp = current.mu.reg_read(UC_X86_REG_ECX)
        text = current.cstr(current.mu.reg_read(UC_X86_REG_EDX))
        events.append(("item", timestamp, text))
        return 0

    def fatal(current):
        """Turn the original fatal report into an observable stop."""
        raise ValueError(current.arg(2))

    m.hook(SAVE, save, 0)
    m.hook(ITEM_HOOK, item, 0)
    m.hook(HEADER_HOOK, header, 0)
    m.hook(0x408090, lambda _current: 0, 0)
    m.hook(0x408A60, fatal, 0)
    m.w32(ITEM_CB, ITEM_HOOK)
    m.w32(HEADER_CB, HEADER_HOOK)

    def reply(oldest, newest, records):
        """Send one synthetic reply to the original handler."""
        packet = bytes([len(records)]) + bytes(4) + struct.pack("<II", oldest, newest)
        packet += b"".join(struct.pack("<I", stamp) + text + b"\0" for stamp, text in records)
        address = m.put(packet)
        value = m.call(REPLY, ecx=address, limit=200000)
        require(m.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "reply did not return")
        require(value == 1, f"reply returned {value}")

    def rows():
        """Read flags, timestamps and text from the original array."""
        return [(m.r32(entries + i * 0x408), m.r32(entries + i * 0x408 + 4),
                 m.cstr(entries + i * 0x408 + 8)) for i in range(m.r32(COUNT))]

    reply(20, 40, [(30, b"thirty"), (10, b"ten"), (0, b"motd"), (20, b"twenty"), (30, b"replacement")])
    require(rows() == [(0, 10, "ten"), (0, 20, "twenty"), (0, 30, "replacement")], str(rows()))
    require(events == [("item", 0, "motd"), ("save", 1, 40), ("header",),
                       ("item", 30, "replacement"), ("item", 20, "twenty")], str(events))
    events.clear()
    m.w32(entries + 2 * 0x408, 42)
    reply(31, 50, [(30, b"again")])
    require(rows()[-1] == (0, 30, "again"), str(rows()))
    require(events == [("save", 3, 50), ("header",)], str(events))
    events.clear()
    m.w32(ITEM_CB, 0)
    m.w32(HEADER_CB, 0)
    reply(0, 60, [])
    require(events == [("save", 0, 60)], str(events))
    events.clear()
    save_result[0] = 0
    reply(0, 70, [(0, b"x")] * 127)
    require(events == [("save", 0, 70)], str(events))
    # A failed cache save does not change the reply handler's successful return.
    # MOVSX treats byte 0x80 as negative; the unsigned 0x100 guard rejects it.
    for count in (0x80, 0xFF):
        bad = m.put(bytes([count]) + bytes(12))
        try:
            m.call(REPLY, ecx=bad, limit=200000)
        except ValueError as error:
            require(error.args == (0x77,), str(error))
        else:
            raise AssertionError(f"count byte {count:#x} did not take the fatal path")
    print("ok: news merge, cutoff, callback order, replacement and signed count guard")


if __name__ == "__main__":
    run()
