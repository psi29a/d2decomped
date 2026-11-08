# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise stock SoundHdr sample loading (FUN_00482970).

Run: uv run python sound_hdr_load.py

Synthetic records and hooked file, cache, allocator and audio boundaries test
the dispatcher and its state transitions, not actual file or audio decoding.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
LOAD = 0x482970
LIMIT = 100000


class Fatal(Exception):
    """The synchronous read failure diagnostic."""


def require(condition, message):
    """Fail on incorrect state, calls or incomplete execution."""
    if not condition:
        raise RuntimeError(message)


def main():
    """Check loaded guards, file failure, cache pressure and both load modes."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d")
    machine = emu.Emu(str(exe))
    records = machine.alloc(0x8E * 2)
    row = records + 0x8E
    machine.w32(0x7BC9A0, records)
    machine.w32(0x7BC9A4, 2)
    events = []
    state = {"open": 1, "evict": 1, "data": 1, "request": 1}

    def simple(name, result=0):
        """Build a hook that records the call and returns a fixed value."""
        def run(_current):
            """Record a dependent call without performing its side effects."""
            events.append(name)
            return result
        return run

    def path(current):
        """Supply a bounded synthetic file path."""
        events.append("path")
        current.mu.mem_write(current.mu.reg_read(UC_X86_REG_EDX), b"sample.wav\0")
        return 0

    def open_file(current):
        """Provide a fake handle when the fixture says the file exists."""
        events.append("open")
        current.w32(current.mu.reg_read(UC_X86_REG_EDX), 0x5555)
        return state["open"]

    def evict(current):
        """Record requested bytes and the two eviction flags."""
        events.append(("evict", current.arg(0), current.arg(1), current.arg(2)))
        return state["evict"]

    def archive(current):
        """Set the synchronous read size and return a fake archive."""
        events.append("archive")
        current.w32(current.arg(0), 40)
        return 0x1111

    def read_file(_current):
        """Return a fake sample pointer or a read failure."""
        events.append("read")
        return 0x2222 if state["data"] else 0

    def request(_current):
        """Return a fake asynchronous request or failure."""
        events.append("request")
        return 0x3333 if state["request"] else 0

    def fatal(current):
        """Catch the synchronous read failure diagnostic."""
        raise Fatal(current.arg(2))

    for address, callback, stack in ((0x482710, path, 0), (0x4068E0, open_file, 0),
                                     (0x406930, simple("size", 40), 0), (0x4068F0, simple("close"), 0),
                                     (0x4824A0, evict, 3), (0x4FAC80, archive, 3), (0x517079, read_file, 3),
                                     (0x482860, simple("touch"), 0), (0x4DF630, simple("decode"), 0),
                                     (0x481720, simple("wait"), 0), (0x40A620, request, 8),
                                     (0x40AA40, simple("async-size", 40), 0), (0x408A60, fatal, 0)):
        machine.hook(address, callback, stack)

    def reset(load_state=0, missing=0, total=20, maximum=100):
        """Reset row, global counters, hook controls and event log."""
        machine.mu.mem_write(row, bytes(0x8E))
        machine.mu.mem_write(row + 0x81, bytes([missing]))
        machine.w32(row + 0x86, load_state)
        machine.w32(0x7BC9BC, 77)
        machine.w32(0x7BC9C4, 0)
        machine.w32(0x7BC9C8, 0)
        machine.w32(0x7BC9CC, maximum)
        machine.w32(0x7BC9D0, total)
        state.update(open=1, evict=1, data=1, request=1)
        events.clear()

    def run(sync, age=0):
        """Call the dispatcher and require it to return."""
        machine.call(LOAD, age, ecx=1, edx=sync, limit=LIMIT)
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "instruction cap")

    for load_state, missing, sync, expected in ((0, 1, 1, []), (2, 0, 1, []), (1, 0, 0, []), (1, 0, 1, ["wait"])):
        reset(load_state, missing)
        run(sync)
        require(events == expected, f"guard events: {events}")
    reset()
    state["open"] = 0
    run(1)
    require(events == ["path", "open"] and machine.read(row + 0x81, 1) == b"\x01", "missing file")
    reset(total=80)
    state["evict"] = 0
    run(1, 1)
    require(events == ["path", "open", "size", "close", ("evict", 20, 1, 1)] and machine.r32(0x7BC9C4) == 77,
            f"cache rejection: {events}")
    reset()
    run(1)
    require(events == ["path", "open", "size", "close", "archive", "read", "touch", "decode"], f"sync: {events}")
    require((machine.r32(row + 0x6C), machine.r32(row + 0x70), machine.r32(row + 0x86), machine.r32(0x7BC9D0))
            == (0x2222, 40, 2, 60), "sync fields")
    reset()
    run(0)
    require(events == ["path", "open", "size", "close", "request", "async-size", "touch"], f"async: {events}")
    require((machine.r32(row + 0x82), machine.r32(row + 0x70), machine.r32(row + 0x86), machine.r32(0x7BC9C8))
            == (0x3333, 40, 1, 1), "async fields")
    reset()
    state["data"] = 0
    try:
        run(1)
    except Fatal as error:
        require(error.args[0] == 0x302, f"read diagnostic: {error.args}")
    else:
        raise RuntimeError("missing read diagnostic")
    reset()
    state["request"] = 0
    run(0)
    require(machine.r32(row + 0x86) == 0 and machine.r32(0x7BC9D0) == 20, "failed async request")
    print("ok: SoundHdr loading guards, cache rejection, sync and async cases")


if __name__ == "__main__":
    main()
