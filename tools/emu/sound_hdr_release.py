# SPDX-License-Identifier: GPL-3.0-or-later
"""Check stock SoundHdr loaded-record release (FUN_004823e0).

Run: uv run python sound_hdr_release.py

Synthetic records exercise unloaded, synchronous and asynchronous cases;
the active-sound getter, list removal, free calls and fatal boundary are hooked.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
RELEASE = 0x4823E0
LIMIT = 10000


class Fatal(Exception):
    """The game's active-sound release diagnostic."""


def require(condition, message):
    """Fail on an unexpected observation or incomplete stock call."""
    if not condition:
        raise RuntimeError(message)


def main():
    """Compare release effects and external calls for each load mode."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d")
    machine = emu.Emu(str(exe))
    records = machine.alloc(0x8E * 2)
    record = records + 0x8E
    machine.w32(0x7BC9A0, records)
    machine.w32(0x7BC9A4, 2)
    events = []
    active = [0]

    def active_sound(_current):
        """Return whether this sound is currently active."""
        events.append(("active",))
        return active[0]

    def remove(current):
        """Record removal from the sound cache list."""
        events.append(("remove", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
        return 0

    def free_async(current):
        """Record asynchronous request release."""
        events.append(("async", current.mu.reg_read(UC_X86_REG_ECX)))
        return 0

    def free_memory(current):
        """Record synchronous sample release and its source line."""
        events.append(("memory", current.mu.reg_read(UC_X86_REG_ECX), current.arg(0)))
        return 0

    def fatal(current):
        """Intercept the active-sound diagnostic before process exit."""
        raise Fatal(current.arg(2))

    machine.hook(0x4DF9D0, active_sound, 0)
    machine.hook(0x516980, remove, 0)
    machine.hook(0x40AB00, free_async, 0)
    machine.hook(0x40B3C0, free_memory, 2)
    machine.hook(0x408A60, fatal, 0)
    for state, async_ptr, is_active in ((0, 0, 0), (1, 0, 0), (2, 0x2222, 0), (1, 0, 1)):
        for offset, value in ((0x6C, 0x1111), (0x70, 123), (0x7C, 42), (0x82, async_ptr), (0x86, state)):
            machine.w32(record + offset, value)
        machine.w32(0x7BC9D0, 1000)
        active[0] = is_active
        events.clear()
        try:
            machine.call(RELEASE, ecx=1, limit=LIMIT)
        except Fatal as error:
            require(is_active and error.args[0] == 0x259, f"fatal: {error.args}")
            require(machine.r32(0x7BC9D0) == 1000 and events == [("active",)], "active case changed state")
            continue
        require(not is_active and machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "unexpected return")
        if state == 0:
            require(not events and machine.r32(0x7BC9D0) == 1000, "unloaded record changed")
            continue
        expected_free = ("async", async_ptr) if async_ptr else ("memory", 0x1111, 0x272)
        require(events == [("active",), ("remove", 0x7BC9B8, 1), expected_free], f"release events: {events}")
        require(machine.r32(0x7BC9D0) == 877, "cache byte count")
        require(all(machine.r32(record + offset) == 0 for offset in (0x6C, 0x7C, 0x82, 0x86)), "record fields")
    print("ok: unloaded, synchronous, asynchronous and active sound release")


if __name__ == "__main__":
    main()
