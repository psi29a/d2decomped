# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared harness for the GAME\\Clients.cpp and GAME\\Roster.cpp oracles: not an oracle itself.

`Rig` is the emulated game.exe 1.14d with the fatal path (0x40_8a60, the shared
assert caller) turned into a `Halt` carrying the source line, the pool entries of
`_monster_world.World` and a `go()` that returns ("ret", eax) or ("halt", line).
`Arena` is one block of emulator memory with a byte-for-byte model copy: a model
reads and writes the copy through the same dword/word/byte helpers and `check`
compares every byte of the emulator block with it after each call, so a write the
model does not expect fails. The arena also hands out blocks for hooked
allocators (`bump`) so the model can predict the pointer the game stored.
"""
import struct

import capstone

import _monster_world as mw
from _monster_world import Mismatch, require
from disasm import ends_function

__all__ = ["ANY", "Arena", "Halt", "Mismatch", "Rig", "require", "signed", "verify"]

MASK = 0xFFFFFFFF
ASSERT = 0x40_8A60
ANY = object()                   # a void function's EAX: not compared
REPORT_STRING = 0x6CC837         # an empty string (NUL padding after "D2.ini") every fatal call site pushes first;
#                                  the source file is not named: the call site (argument 1) is the only identification


class Halt(Exception):
    """game.exe took the fatal-error path; args are (source line, address the report was called from)."""


def signed(value):
    """A 32-bit value as a signed int."""
    value &= MASK
    return value - (1 << 32) if value & 0x80000000 else value


class Rig(mw.World):
    """The emulated game with the fatal path hooked and a call wrapper that reports it."""

    def __init__(self):
        """Hook the assert caller; 0x40_8090 (returns its return address) and the rest run as stock."""
        super().__init__()
        mw.INSTRUCTION_LIMIT = 200000
        self.called = set()
        self.m.hook(ASSERT, self.fatal, 0)

    def fatal(self, current):
        """0x40_8a60 (empty string, call site from 0x40_8090, line): stop with the line and the call site."""
        require(current.arg(0) == REPORT_STRING, "fatal report passes an unexpected first argument")
        raise Halt(current.arg(2), current.arg(1))

    def extent(self, start):
        """(start, end) of the function at `start`, ended the way disasm.py ends one (a ret before padding or a prologue)."""
        size = 0x2000
        image = self.m.read(start, size)
        for ins in capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32).disasm(image, start):
            if ends_function(image, start, ins):
                return start, ins.address + ins.size
        return start, start + size

    def go(self, address, *args, ecx=0, edx=0, regs=None, nstack=None, keep=("ebx", "esi", "edi"), fatal_from=()):
        """One call: ("ret", eax) or ("halt", line). Registers in `keep` must come back unchanged.

        EBX/ESI/EDI start as marker values (or the `regs` overrides) so a function that clobbers one is seen.
        A fatal report must come from the function itself or from one of the callees in `fatal_from`, so a
        callee's fatal with the same line is not taken for the function's own.
        """
        self.called.add(address)
        start = {"ebx": 0x1BAD0001, "esi": 0x1BAD0002, "edi": 0x1BAD0003}
        start.update(regs or {})
        try:
            value = self.run(address, *args, ecx=ecx, edx=edx, regs=start, nstack=nstack)
        except Halt as halt:
            line, site = halt.args
            spans = [self.extent(entry) for entry in (address, *fatal_from)]
            require(any(lo <= site < hi for lo, hi in spans),
                    f"{address:#x}: fatal line {line:#x} reported from {site:#x}, outside {[(hex(lo), hex(hi)) for lo, hi in spans]}")
            return ("halt", line)
        for name in keep:
            now = self.m.mu.reg_read(mw.REGISTERS[name])
            require(now == start[name], f"{address:#x} changed {name}: {start[name]:#x} -> {now:#x}")
        return ("ret", value)


class Arena:
    """A block of emulator memory plus a model copy; `bump` hands out sub-blocks like a hooked allocator."""

    def __init__(self, rig, rng, size=0x8000, pool=0x6000):
        """Fill the block with random bytes (so fields the model ignores are not zero) and mirror it."""
        self.rig, self.rng, self.size = rig, rng, size
        self.base = rig.m.alloc(size + 64)
        self.data = bytearray(rng.randbytes(size))
        self.pool_start = pool
        self.pool = pool                        # next free offset for hooked allocations (the model's counter)
        self.pool_hook = pool                   # the same counter as the emulated hook sees
        rig.write(self.base, self.data)

    def at(self, offset):
        """Absolute address of an offset in the arena."""
        return self.base + offset

    def _off(self, address, width):
        """Offset of an absolute address; reads and writes outside the arena are a defect in the model."""
        off = address - self.base
        require(0 <= off and off + width <= self.size, f"model access outside the arena: {address:#x}")
        return off

    def r32(self, address):
        """Model dword."""
        return struct.unpack_from("<I", self.data, self._off(address, 4))[0]

    def r16(self, address):
        """Model word."""
        return struct.unpack_from("<H", self.data, self._off(address, 2))[0]

    def r8(self, address):
        """Model byte."""
        return self.data[self._off(address, 1)]

    def w(self, address, value, width=4):
        """Model write of `width` bytes."""
        struct.pack_into({1: "<B", 2: "<H", 4: "<I"}[width], self.data, self._off(address, width),
                         value & ((1 << 8 * width) - 1))

    def put(self, address, value, width=4):
        """Set-up write: the model copy and the emulator memory both change."""
        self.w(address, value, width)
        self.rig.write(address, struct.pack({1: "<B", 2: "<H", 4: "<I"}[width], value & ((1 << 8 * width) - 1)))

    def put_bytes(self, address, data):
        """Set-up write of raw bytes to both copies."""
        off = self._off(address, len(data))
        self.data[off:off + len(data)] = data
        self.rig.write(address, data)

    def hook_alloc(self, size):
        """What the emulated allocator hook returns: the next block of the pool, 16-byte aligned."""
        address = self.at(self.pool_hook)
        self.pool_hook += (size + 15) & ~15
        return address

    def model_alloc(self, size):
        """What the model expects the allocator to have returned (same rule, its own counter)."""
        address = self.at(self.pool)
        self.pool += (size + 15) & ~15
        return address

    def sync_pool(self):
        """Reset both counters (set-up)."""
        self.pool = self.pool_hook = self.pool_start

    def check(self, label):
        """Every byte of the emulator block equals the model copy; the allocator counters agree."""
        got = self.rig.m.read(self.base, self.size)
        if got != bytes(self.data):
            where = next(i for i in range(self.size) if got[i] != self.data[i])
            raise Mismatch(f"{label}: arena differs at {where:#x}: game.exe {got[where]:#x}, model {self.data[where]:#x}")
        require(self.pool == self.pool_hook, f"{label}: allocations differ: game.exe {self.pool_hook:#x}, model {self.pool:#x}")


def verify(rig, fixture, label, fn, want, **kw):
    """Run the stock function and compare return/halt, the hook log and every arena byte with the model.

    `fixture` has `.a` (the Arena) and `.log` (the model's expected hook log, cleared here after the check);
    `want` is ("ret", value | ANY) or ("halt", line). `args=` gives the stack arguments, `mask=` keeps only those bits of a narrow
    return in both the game's value and the expected one (the rest of EAX is not part of it), the rest go to `Rig.go`.
    """
    rig.log.clear()
    mask = kw.pop("mask", None)
    got = rig.go(fn, *kw.pop("args", ()), **kw)
    if mask is not None:                                   # a byte or word return: the rest of EAX is not part of it
        if got[0] == "ret":
            got = ("ret", got[1] & mask)
        if want[0] == "ret" and want[1] is not ANY:
            want = ("ret", want[1] & mask)
    if want[0] == "ret" and want[1] is ANY and got[0] == "ret":
        got = ("ret", ANY)
    require(got == want, f"{label}: game.exe {got!r}, model {want!r}")
    require(list(rig.log) == fixture.log, f"{label}: log {list(rig.log)!r}, model {fixture.log!r}")
    fixture.a.check(label)
    fixture.log = []
    return 1
