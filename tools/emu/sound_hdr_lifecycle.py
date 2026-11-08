# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise SoundHdr initialization, touch, cleanup and shutdown wrappers.

Run: uv run python sound_hdr_lifecycle.py

Stock game.exe 1.14d executes the four wrappers; subsystem calls, record
release, list operations and memory free are intercepted and recorded.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
INITIALIZE, TOUCH, RELEASE, SHUTDOWN = 0x482260, 0x482860, 0x4828B0, 0x482940
GLOBALS = 0x7BC9A0
LIMIT = 10000


def require(condition, message):
    """Reject an unexpected event, result or instruction-cap stop."""
    if not condition:
        raise RuntimeError(message)


def main():
    """Test getter guards, subsystem order and record cleanup."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d")
    machine = emu.Emu(str(exe))
    events = []
    enabled = [0]

    def sound_enabled(_current):
        """Return the selected value of the subsystem getter."""
        events.append((0x513B50,))
        return enabled[0]

    def event(address):
        """Make a hook that records the callee and its register arguments."""
        def record(current):
            """Record the call without entering the dependent subsystem."""
            events.append((address, current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
            return 0
        return record

    machine.hook(0x513B50, sound_enabled, 0)
    for address in (0x481950, 0x4E0050, 0x4B9D00, 0x4DCA30, 0x4CA280, 0x515CE0,
                    0x4DF800, 0x4BAAB0, 0x4B9D20, 0x4E00C0, 0x4823E0, 0x516980, 0x516A70):
        machine.hook(address, event(address), 0)

    def free(current):
        """Record the allocator's fastcall pointer and free-site line."""
        events.append((0x40B3C0, current.mu.reg_read(UC_X86_REG_ECX), current.arg(0)))
        return 0

    machine.hook(0x40B3C0, free, 2)

    def call(address, ecx=0):
        """Run a wrapper to normal return."""
        machine.call(address, ecx=ecx, limit=LIMIT)
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{address:#x}: instruction cap")

    for enabled[0] in (0, 1):
        for offset in (0x1C, 0x20, 0x24, 0x28):
            machine.w32(GLOBALS + offset, 0x1234)
        events.clear()
        call(INITIALIZE, 0x9988)
        expected = [0x513B50] + ([0x481950, 0x4E0050, 0x4B9D00, 0x4DCA30, 0x4CA280, 0x515CE0] if enabled[0] else [])
        require([entry[0] for entry in events] == expected, f"initialize order: {events}")
        if enabled[0]:
            require(events[2][1] == 0x9988 and events[-1][1] == 0xFF, f"initialize args: {events}")
        require(all(machine.r32(GLOBALS + offset) == (0 if enabled[0] else 0x1234) for offset in (0x1C, 0x20, 0x24, 0x28)), "initialize globals")

    records = machine.alloc(0x8E * 4)
    machine.w32(GLOBALS, records)
    machine.w32(GLOBALS + 4, 4)
    machine.w32(GLOBALS + 0x1C, 0x76543210)
    events.clear()
    call(TOUCH, 2)
    require(machine.r32(records + 2 * 0x8E + 0x7C) == 0x76543210, "touch timestamp")
    require(events == [(0x516980, GLOBALS + 0x18, 2), (0x516A70, GLOBALS + 0x18, 2)], f"touch events: {events}")

    machine.w32(GLOBALS + 8, 0x12345000)
    machine.w32(GLOBALS + 12, 9)
    machine.w32(GLOBALS + 0x10, 7)
    machine.w32(GLOBALS + 0x14, 8)
    events.clear()
    call(RELEASE)
    require(len(events) == 5 and events[0] == (0x40B3C0, 0x12345000, 0x181)
            and [entry[:2] for entry in events[1:4]] == [(0x4823E0, 1), (0x4823E0, 2), (0x4823E0, 3)]
            and events[4] == (0x40B3C0, records, 0x18E), f"release events: {events}")
    require(all(machine.r32(GLOBALS + offset) == 0 for offset in (0, 4, 8, 12, 0x10, 0x14)), "release globals")
    events.clear()
    call(RELEASE)
    require(not events, f"repeat release: {events}")

    for enabled[0] in (0, 1):
        events.clear()
        call(SHUTDOWN)
        expected = [0x513B50] + ([0x4DF800, 0x4BAAB0, 0x4B9D20, 0x4E00C0] if enabled[0] else [])
        require([entry[0] for entry in events] == expected, f"shutdown order: {events}")
    print("ok: SoundHdr initialize, touch, release and shutdown")


if __name__ == "__main__":
    main()
