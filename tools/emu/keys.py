# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's key bindings as an oracle (docs/research/re/controls.md).

Prints FUN_00469c20's result for every function, slot and key 0..0x104 from
the defaults (0x712220): assigned, message, a hash of the table; then
FUN_00469de0's string id for every key. Diff with the port:

    uv run python keys.py > exe.txt
    ../../build/tests/test_keyconfig --dump | diff - exe.txt
"""
import struct

from unicorn.x86_const import UC_X86_REG_ECX

import emu

e = emu.Emu()
defaults = e.read(0x712220, 0x474)
message = e.alloc(4)
out = []
for action in range(0x39):
    for slot in range(2):
        for key in range(0x105):
            e.mu.mem_write(0x7a6f90, defaults)
            e.w32(message, 0)
            assigned = e.call(0x469c20, key, message, ecx=action, edx=slot) & 1
            total = 0
            for byte in e.read(0x7a6f90, 0x474):
                total = (total * 31 + byte) & 0xFFFFFFFF
            out.append("%x %d %x %d %x %08x" % (action, slot, key, assigned, e.r16(message), total))
# The name lookup (FUN_00524a30) cut off: it hands back its string id.
e.hook(0x524a30, lambda m: 0x40000000 | m.mu.reg_read(UC_X86_REG_ECX), 0)
for key in list(range(0x105)) + [0xFFFF]:
    e.mu.mem_write(0x7a6f90, struct.pack("<iHi", 0, key, 0))
    name = e.call(0x469de0, ecx=0, edx=0)
    out.append("name %x %x" % (key, name & 0xFFFF if name & 0x40000000 else 0))
print("\n".join(out))
