# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared harness for the OBJECTS oracles (objects_*.py): not an oracle itself.

It builds one emulated game.exe 1.14d and gives each oracle:

  `Arena`  a 24 KiB block of emulated memory with a Python mirror. A case fills
           it with random bytes, lays records out in it with `new`, writes the
           fields it sets with `w32` and friends, and the model changes a
           *clone* of the mirror. After the stock call the whole block in the
           emulator is compared with the clone, so a byte the function was not
           meant to touch fails the case as surely as a wrong one.
  `World`  the machine, the call log and the hooks. `rec` hooks a callee that
           is not under test: it logs `(name, registers..., stack args...,
           extra...)` and returns a value both sides compute with `mix`.
           `call` runs one stock function with sentinel values in EBX / ESI /
           EDI / EBP and checks, after a normal return, that they came back
           and that `ret n` popped exactly `pops` stack dwords. The fatal-error
           halt (FUN_00408a60) is hooked: it logs the line number and stops
           the emulation.
  `check`  compares the return value, the ordered callee log, the halt line
           and the arena.

Callees that are not claimed are written `0x12_3456` in the oracles so the gap
map counts only the functions an oracle traces. No MPQs are read.
"""
import hashlib
import struct
import zlib
from pathlib import Path

from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDI,
                               UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP)

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
M32 = 0xFFFFFFFF
LIMIT = 200000
ARENA_SIZE = 0x6000
GET_IP, ERR_HALT = 0x40_8090, 0x40_8A60
REGS = {"eax": UC_X86_REG_EAX, "ecx": UC_X86_REG_ECX, "edx": UC_X86_REG_EDX, "ebx": UC_X86_REG_EBX,
        "esi": UC_X86_REG_ESI, "edi": UC_X86_REG_EDI}
SENTINELS = {"ebx": 0xB0B0B0B0, "esi": 0x51515151, "edi": 0xD1D1D1D1}


class Mismatch(Exception):
    """The emulated function and the model disagree."""


def require(condition, message):
    """Stop the probe when an observation is absent or differs."""
    if not condition:
        raise Mismatch(message if len(message) < 700 else message[:700] + "...")


def s32(value):
    """A dword as a signed integer."""
    value &= M32
    return value - (1 << 32) if value & 0x80000000 else value


def mix(name, *values):
    """A deterministic 32-bit value standing in for the hooked callee `name`'s result."""
    values = [v & M32 for v in values]
    return zlib.crc32(struct.pack(f"<{len(values)}I", *values), zlib.crc32(name.encode())) & M32


class Arena:
    """A block of emulated memory plus its Python mirror; records are laid out at absolute addresses."""

    def __init__(self, base, mem=None):
        """Wrap `base` (an address in the emulator); `mem` is the mirror bytes."""
        self.base = base
        self.mem = bytearray(mem if mem is not None else ARENA_SIZE)
        self.top = 0x100

    def reset(self, rng):
        """Fill with random bytes and forget every record."""
        self.mem[:] = rng.randbytes(ARENA_SIZE)
        self.top = 0x100

    def clone(self):
        """A copy the model can change."""
        other = Arena(self.base, self.mem)
        other.top = self.top
        return other

    def new(self, size):
        """Reserve `size` bytes (16-aligned) and return their absolute address."""
        address = self.base + self.top
        self.top = (self.top + size + 15) & ~15
        require(self.top <= ARENA_SIZE, "arena exhausted")
        return address

    def _at(self, address, size):
        """Mirror offset of an absolute address, checked against the block."""
        offset = address - self.base
        require(0 <= offset <= ARENA_SIZE - size, f"address {address:#x} outside the arena")
        return offset

    def r32(self, address):
        """Unsigned dword."""
        offset = self._at(address, 4)
        require(len(self.mem) - offset >= 4, "short read")
        return struct.unpack_from("<I", self.mem, offset)[0]

    def s32(self, address):
        """Signed dword."""
        offset = self._at(address, 4)
        require(len(self.mem) - offset >= 4, "short read")
        return struct.unpack_from("<i", self.mem, offset)[0]

    def r16(self, address):
        """Unsigned word."""
        offset = self._at(address, 2)
        require(len(self.mem) - offset >= 2, "short read")
        return struct.unpack_from("<H", self.mem, offset)[0]

    def r8(self, address):
        """Unsigned byte."""
        return self.mem[self._at(address, 1)]

    def w32(self, address, value):
        """Store a dword."""
        struct.pack_into("<I", self.mem, self._at(address, 4), value & M32)

    def w16(self, address, value):
        """Store a word."""
        struct.pack_into("<H", self.mem, self._at(address, 2), value & 0xFFFF)

    def w8(self, address, value):
        """Store a byte."""
        self.mem[self._at(address, 1)] = value & 0xFF

    def cstr(self, address):
        """The NUL-terminated bytes at `address`."""
        offset = self._at(address, 1)
        end = self.mem.index(0, offset)
        return bytes(self.mem[offset:end])

    def put(self, address, data):
        """Store raw bytes."""
        offset = self._at(address, len(data))
        self.mem[offset:offset + len(data)] = data

    def first_difference(self, other):
        """A short description of the first byte where `other` differs, or None."""
        for index, (a, b) in enumerate(zip(self.mem, other.mem)):
            if a != b:
                return f"arena +{index:#x} (address {self.base + index:#x}): {a:#04x} != {b:#04x}"
        return None


class World:
    """One emulated game.exe with the halt hook, the call log and an arena."""

    def __init__(self, real=()):
        """Build the machine (requires stock game.exe 1.14d) and hook the fatal-error halt.

        `real` lists game.exe functions a second machine runs for real instead of hooking.
        """
        exe = Path(emu.data_path()) / "bin" / "game.exe"
        require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d (1.14.3.71)")
        self.m = emu.Emu(str(exe), real=real)
        self.arena = Arena(self.m.alloc(ARENA_SIZE))
        self.log = []
        self.halts = []
        self.queue = []         # pointers the allocator hook hands out, in order
        self.cases = 0
        self.m.hook(GET_IP, lambda e: 0, 0)
        self.m.hook(ERR_HALT, self._halt, 0)

    def _halt(self, current):
        """FUN_00408a60 (error halt): note the line number (third argument) and stop the emulation."""
        self.halts.append(current.arg(2))
        current.mu.emu_stop()
        return 0

    def rec(self, address, name, nstack, regs=(), ret=None, peek=None):
        """Hook the callee at `address`; log (name, regs..., stack args..., peek...) and return `ret`.

        `ret` is an int, a function of the logged entry, or None for `mix(name, regs..., args...)`.
        `peek(emulator, args, regvalues)` returns a tuple of extra values for the log only.
        """
        def hook(current):
            """Record one call of the hooked callee."""
            values = tuple(current.mu.reg_read(REGS[r]) for r in regs)
            args = tuple(current.arg(i) for i in range(nstack))
            extra = tuple(peek(current, args, values)) if peek else ()
            entry = (name,) + values + args + extra
            self.log.append(entry)
            if callable(ret):
                return ret(entry) & M32
            return mix(name, *values, *args) if ret is None else ret & M32
        self.m.hook(address, hook, nstack)

    def fake_function(self, name, nstack, regs=("ecx", "edx"), ret=None):
        """A callable stand-in outside the arena for a function pointer in a game.exe table; returns its address."""
        address = self.m.alloc(16)
        self.rec(address, name, nstack, regs, ret)
        return address

    def sync(self):
        """Copy the arena mirror into the emulator."""
        self.m.mu.mem_write(self.arena.base, bytes(self.arena.mem))

    def call(self, address, *args, ecx=0, edx=0, regs=None, pops=None, keep=("ebx", "esi", "edi", "ebp")):
        """Run one stock function; returns (eax, halted).

        `pops` is the number of stack dwords the callee's `ret n` should drop (default: all arguments).
        After a normal return the registers in `keep` must be back where they started.
        """
        self.log = []
        self.halts = []
        start = dict(SENTINELS)
        start.update(regs or {})
        top = emu.STACK + emu.STACK_SIZE - 0x1000
        eax = self.m.call(address, *args, ecx=ecx, edx=edx, regs=start, limit=LIMIT)
        mu = self.m.mu
        if self.halts:
            return eax, True
        require(mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{address:#x}: did not return within {LIMIT} instructions")
        popped = len(args) if pops is None else pops
        want_esp = top - 4 * len(args) + 4 * popped
        got_esp = mu.reg_read(UC_X86_REG_ESP)
        require(got_esp == want_esp, f"{address:#x}: ret popped {(got_esp - top + 4 * len(args)) // 4} dwords, expected {popped}")
        for name in keep:
            now = mu.reg_read(UC_X86_REG_EBP) if name == "ebp" else mu.reg_read(REGS[name])
            was = 0 if name == "ebp" else start[name]
            require(now == was, f"{address:#x}: {name} not preserved ({now:#x} != {was:#x})")
        return eax, False

    def go(self, label, address, *args, ecx=0, edx=0, regs=None, pops=None, build=None, keep=("ebx", "esi", "edi", "ebp")):
        """Sync the arena, run the stock function and compare with `build(clone)` -> (ret, log, halt line)."""
        model = self.arena.clone()
        want_ret, want_log, want_halt = build(model)
        self.sync()
        eax, halted = self.call(address, *args, ecx=ecx, edx=edx, regs=regs, pops=pops, keep=keep)
        self.check(label, eax, halted, model, want_log, want_ret, want_halt)

    def check(self, label, eax, halted, model_arena, want_log, want_ret=None, want_halt=None):
        """Compare the call just made with the model: halt line, return value, callee log and the whole arena."""
        self.cases += 1
        if want_halt is not None:
            require(halted and self.halts == [want_halt], f"{label}: halts {self.halts}, wanted line {want_halt:#x}")
        else:
            require(not halted, f"{label}: unexpected fatal error at line {[hex(h) for h in self.halts]}")
            if want_ret is not None:
                require(eax == want_ret & M32, f"{label}: returned {eax:#x}, model {want_ret & M32:#x}")
        require(self.log == want_log, f"{label}: callee log\n  got  {self.log!r}\n  want {want_log!r}")
        now = Arena(self.arena.base, self.m.read(self.arena.base, ARENA_SIZE))
        difference = now.first_difference(model_arena)
        require(difference is None, f"{label}: memory differs at {difference}")
