# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared harness for the monster_*.py oracles: not an oracle itself.

It builds one emulated game.exe 1.14d, hooks the pool allocator entries
(allocate 0x40_b430, release 0x40_b480; both log their arguments), gives each
oracle `World.logger` (a hooked callee that logs its registers and stack
arguments and returns a scripted value), `World.run` (one bounded stock call
that checks the bytes the `ret n` popped) and `drive` (the command line with
`--mutation-check`). Callee addresses are written with an underscore
(0x40_b430) so the gap map counts only the functions an oracle claims.
"""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EDX,
                               UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP)

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
POOL_ALLOC = 0x40_B430
POOL_FREE = 0x40_B480
TABLE_PTR = 0x744304
INSTRUCTION_LIMIT = 20000
GARBAGE = 0xA5
REGISTERS = {"ecx": UC_X86_REG_ECX, "edx": UC_X86_REG_EDX, "ebx": UC_X86_REG_EBX, "esi": UC_X86_REG_ESI,
             "edi": UC_X86_REG_EDI, "eax": UC_X86_REG_EAX}


class Mismatch(Exception):
    """The emulated function and the model disagree."""


class Fault(Exception):
    """The emulated function read or wrote unmapped memory."""


def require(condition, message):
    """Stop the probe when an observation is absent or differs."""
    if not condition:
        raise Mismatch(message)


def silent_bad(_machine, _mu, _access, _addr, _size, _value, _user):
    """Memory-fault handler that does not print: the oracle reports the fault itself."""
    return False


class World:
    """One emulated game.exe with hooked pool entries and a call log."""

    def __init__(self):
        """Build the machine and hook the allocator; the log starts empty."""
        exe = Path(emu.data_path()) / "bin" / "game.exe"
        require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d (1.14.3.71)")
        original = emu.Emu._bad
        emu.Emu._bad = silent_bad
        try:
            self.m = emu.Emu(str(exe))
        finally:
            emu.Emu._bad = original
        self.log = []
        self.script = {}
        self.m.hook(POOL_ALLOC, self.pool_alloc, 3)
        self.m.hook(POOL_FREE, self.pool_free, 3)

    def file_of(self, pointer):
        """The source-file string a pool call names."""
        return self.m.cstr(pointer)

    def pool_alloc(self, current):
        """Pool allocate (ECX pool, EDX size, stack: file, line, flag): log it, return garbage-filled memory."""
        pool = current.mu.reg_read(UC_X86_REG_ECX)
        size = current.mu.reg_read(UC_X86_REG_EDX)
        self.log.append(("alloc", pool, size, self.file_of(current.arg(0)), current.arg(1), current.arg(2)))
        block = current.alloc(size, zero=False)
        current.mu.mem_write(block, bytes([GARBAGE]) * size)
        return block

    def pool_free(self, current):
        """Pool release (ECX pool, EDX block, stack: file, line, flag): log it, return 1."""
        pool = current.mu.reg_read(UC_X86_REG_ECX)
        block = current.mu.reg_read(UC_X86_REG_EDX)
        self.log.append(("free", pool, block, self.file_of(current.arg(0)), current.arg(1), current.arg(2)))
        return 1

    def logger(self, address, nstack, name, ret=0, regs=("ecx", "edx")):
        """Hook a callee: log (name, registers..., stack arguments...) and return `ret` (a value or fn(current)).

        A register named "dl" logs only the low byte of EDX."""
        def hook(current):
            """Record the call and hand back the scripted result."""
            seen = tuple(current.mu.reg_read(UC_X86_REG_EDX) & 0xFF if r == "dl" else current.mu.reg_read(REGISTERS[r])
                         for r in regs)
            self.log.append((name,) + seen + tuple(current.arg(i) for i in range(nstack)))
            return ret(current) if callable(ret) else ret
        self.m.hook(address, hook, nstack)

    def slot(self, fn, nstack):
        """A fresh code address that runs `fn(emu) -> eax` and pops `nstack` args (a callback target)."""
        address = emu.STUB + 16 * (len(self.m.slots) + 1)
        self.m.slots[address] = ("slot", lambda current: (fn(current), nstack))
        return address

    def run(self, address, *args, ecx=0, edx=0, regs=None, nstack=None):
        """One bounded stock call; returns eax. Checks that it returned and that `ret n` popped `nstack` args."""
        machine = self.m
        sp0 = emu.STACK + emu.STACK_SIZE - 0x1000 - 4 * (len(args) + 1)
        self.log.clear()
        try:
            value = machine.call(address, *args, ecx=ecx, edx=edx, limit=INSTRUCTION_LIMIT, regs=regs)
        except RuntimeError as err:
            raise Fault(str(err)) from None
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{address:#x} did not return")
        popped = machine.mu.reg_read(UC_X86_REG_ESP) - sp0 - 4
        want = len(args) if nstack is None else nstack
        require(popped == 4 * want, f"{address:#x} popped {popped} bytes, expected {4 * want}")
        return value

    def write(self, address, data):
        """Write bytes to emulator memory."""
        self.m.mu.mem_write(address, bytes(data))

    def read_words(self, address, count):
        """Read `count` little-endian dwords."""
        return list(struct.unpack(f"<{count}I", self.m.read(address, 4 * count)))

    def put_words(self, address, words):
        """Write dwords."""
        self.write(address, struct.pack(f"<{len(words)}I", *[w & 0xFFFFFFFF for w in words]))


def drive(doc, module, run_all, description):
    """Command line: run the oracle, or with --mutation-check require every module.MUTATIONS defect to fail."""
    parser = argparse.ArgumentParser(description=doc, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mutation-check", action="store_true", help="require every deliberate model defect to fail")
    options = parser.parse_args()
    if options.mutation_check:
        for name in module.MUTATIONS:
            module.mutation = name
            try:
                run_all()
            except Mismatch:
                print(f"caught: {name}")
                continue
            sys.exit(f"NOT caught: {name}")
        print(f"ok: {len(module.MUTATIONS)} deliberate model defects all caught")
        return
    print(f"ok: {run_all()} {description}")
