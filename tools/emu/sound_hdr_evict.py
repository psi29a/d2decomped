# SPDX-License-Identifier: GPL-3.0-or-later
"""Check stock SoundHdr cache eviction (FUN_004824a0).

Run: uv run python sound_hdr_evict.py

Synthetic cache nodes and rows exercise the two-pass and age rules. Active
checks and record release are hooked; the release hook unlinks the test node
and updates the cache byte count as the real release does.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
EVICT = 0x4824A0
LIMIT = 100000


def require(condition, message):
    """Stop on a mismatch or a call that does not complete."""
    if not condition:
        raise RuntimeError(message)


def main():
    """Check pin/cache, second-pass, age and active sound handling."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d")
    machine = emu.Emu(str(exe))
    records = machine.alloc(0x8E * 4)
    nodes = machine.alloc(8 * 3)
    machine.w32(0x7BC9A0, records)
    machine.w32(0x7BC9A4, 4)
    calls = []
    active = set()
    available = []

    def check_active(current):
        """Return whether the requested row is currently playing."""
        sound = current.mu.reg_read(UC_X86_REG_ECX)
        calls.append(("active", sound))
        return int(sound in active)

    def release(current):
        """Unlink one fixture node and update the recorded byte total."""
        sound = current.mu.reg_read(UC_X86_REG_ECX)
        calls.append(("release", sound))
        available.remove(sound)
        machine.w32(0x7BC9B8, nodes + (available[0] - 1) * 8 if available else 0)
        for index, item in enumerate(available):
            machine.w32(nodes + (item - 1) * 8 + 4, nodes + (available[index + 1] - 1) * 8 if index + 1 < len(available) else 0)
        machine.w32(0x7BC9D0, machine.r32(0x7BC9D0) - 25)
        return 0

    machine.hook(0x4DF9D0, check_active, 0)
    machine.hook(0x4823E0, release, 0)

    def run(request, second, age, pinned=(), cached=(), playing=(), times=(0, 0, 0)):
        """Reset a three-node fixture, then return outcome and call trace."""
        available[:] = [1, 2, 3]
        active.clear()
        active.update(playing)
        calls.clear()
        machine.w32(0x7BC9B8, nodes)
        machine.w32(0x7BC9BC, 1000)
        machine.w32(0x7BC9D0, 75)
        for sound in available:
            node = nodes + (sound - 1) * 8
            row = records + sound * 0x8E
            machine.w32(node, sound)
            machine.w32(node + 4, node + 8 if sound < 3 else 0)
            machine.w32(row + 0x8A, int(sound in pinned))
            machine.mu.mem_write(row + 0x4C, bytes([int(sound in cached)]))
            machine.w32(row + 0x7C, times[sound - 1])
        result = machine.call(EVICT, request, second, age, limit=LIMIT)
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "eviction stopped at instruction cap")
        return result, list(calls), machine.r32(0x7BC9D0)

    result, calls, total = run(25, 0, 0, pinned=(1,), cached=(2,))
    require((result, calls, total) == (1, [("active", 3), ("release", 3)], 50), "first-pass pin/cache")
    result, calls, total = run(25, 1, 0, pinned=(1,), cached=(2, 3))
    require((result, calls, total) == (1, [("active", 1), ("release", 1)], 50), "second-pass override")
    result, calls, total = run(25, 0, 1, times=(251, 250, 0))
    require((result, calls, total) == (1, [("active", 2), ("release", 2)], 50), "750-tick boundary")
    result, calls, total = run(25, 0, 0, playing=(1, 2, 3))
    require((result, calls, total) == (0, [("active", 1), ("active", 2), ("active", 3)], 75), "all active")
    result, calls, total = run(0, 0, 0)
    require((result, calls, total) == (1, [], 75), "zero bytes requested")
    result, calls, total = run(76, 0, 0)
    require((result, calls, total) == (1, [], 75), "unsigned target underflow")
    print("ok: SoundHdr eviction pass, age and active-sound cases")


if __name__ == "__main__":
    main()
