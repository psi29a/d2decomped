# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared rig for the DUNGEON\\Dungeon.cpp oracles (dungeon_*.py): not run by itself.

A Rig starts stock game.exe 1.14d in Unicorn, maps one random-filled arena
(ARENA_SIZE bytes of room1, room2, array and list records), hooks the fatal
error path and the callees a probe does not claim, and runs one function at a
time on a random copy of the arena. A hand-written Model (plain Python, the
claim) is run on the same bytes; afterwards the rig compares:

  - the return value (when the model gives one) to the width the model gives,
  - the ordered callee log: name, register arguments, stack arguments,
  - the whole arena, and every block the hooked allocator handed out,
  - the fatal-error line (hook of 0x40_8a60), when the model expects one,
  - the stack pointer after `ret n` (the bytes the callee pops).

A callee that is hooked returns mix(name, its arguments), so a wrong argument
changes both the log and the result. The model's fatal assert is `Halt(line)`.
No MPQs are read.
"""
import hashlib
import random
import struct
import zlib
from pathlib import Path

from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EDX,
                               UC_X86_REG_ESI, UC_X86_REG_ESP)

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
M32 = 0xFFFFFFFF
LIMIT = 200000
ARENA_SIZE = 0x4000
ALLOC_FILE = 0x6E_7D04           # the source-file string the arena allocator calls pass
GET_IP, ERR_HALT = 0x40_8090, 0x40_8A60
ALLOC, FREE = 0x40_B430, 0x40_B480
ROUNDS = 24


class Mismatch(Exception):
    """The stock code and the model disagree."""


class Halt(Exception):
    """The model's fatal assert: the line number game.exe's error halt is given."""

    def __init__(self, line):
        """Remember the source line."""
        super().__init__(line)
        self.line = line


def require(condition, message):
    """Raise Mismatch with `message` unless `condition` holds."""
    if not condition:
        raise Mismatch(message)


def s32(v):
    """Signed view of a 32-bit value."""
    v &= M32
    return v - (1 << 32) if v >> 31 else v


def mix(name, *values):
    """A deterministic 32-bit result a hooked callee returns for these arguments."""
    blob = name.encode() + struct.pack(f"<{len(values)}I", *[v & M32 for v in values])
    return zlib.crc32(blob)


class Model:
    """The state a model works on: a copy of the arena, the allocator's blocks and the expected log."""

    def __init__(self, rig, img, outs):
        """Keep the arena copy `img` and the hooked callees' results `outs` (in call order)."""
        self.rig = rig
        self.regions = [(rig.arena, img)]
        self.blocks = []            # (pointer, bytearray) the allocator handed out
        self.log = []
        self.outs = outs

    def region(self, addr, size):
        """The bytearray and offset holding [addr, addr+size), or a Mismatch for a model slip."""
        for base, buf in self.regions:
            if base <= addr and addr + size <= base + len(buf):
                return buf, addr - base
        raise Mismatch(f"model touched unmapped {addr:#x}")

    def r(self, addr, size=4):
        """Little-endian unsigned read."""
        buf, off = self.region(addr, size)
        return int.from_bytes(buf[off:off + size], "little")

    def w(self, addr, value, size=4):
        """Little-endian write."""
        buf, off = self.region(addr, size)
        buf[off:off + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little")

    def call(self, name, *args):
        """Log a hooked callee; its result is what the stock run's hook returned (the arguments are checked by the log)."""
        self.log.append((name,) + tuple(a if isinstance(a, bytes) else a & M32 for a in args))
        return self.out_of(len(self.log) - 1)

    def out_of(self, index):
        """The result of the index-th hooked call of the stock run; a Mismatch if the model calls more."""
        require(index < len(self.outs), f"model made call {index} but the stock code made {len(self.outs)}")
        return self.outs[index]

    def alloc(self, pool, size, line, file=ALLOC_FILE):
        """Log the arena allocator (0x40_b430) and return the pointer the stock run got (`file`: the __FILE__ string it was passed)."""
        self.log.append(("alloc", pool & M32, size & M32, file, line, 0))
        pointer = self.out_of(len(self.log) - 1)
        buf = bytearray(size)
        self.blocks.append((pointer, buf))
        self.regions.append((pointer, buf))
        return pointer

    def realloc(self, pool, old, size, line, file):
        """Log the arena realloc (0x40_b4b0: registers pool, old block; stack size, file, line, 0) and return the stock block."""
        self.log.append(("realloc", pool & M32, old & M32, size & M32, file, line, 0))
        pointer = self.out_of(len(self.log) - 1)
        buf = bytearray(size)
        self.blocks.append((pointer, buf))
        self.regions.append((pointer, buf))
        return pointer

    def free(self, pool, pointer, line, file=ALLOC_FILE):
        """Log the arena free (0x40_b480) (`file`: the __FILE__ string it was passed)."""
        self.log.append(("free", pool & M32, pointer & M32, file, line, 0))


class Rig:
    """The emulator, the arena, the hooks and the case runner."""

    def __init__(self):
        """Load stock game.exe, map the arena and hook the fatal path, allocator and callees."""
        exe = Path(emu.data_path()) / "bin" / "game.exe"
        require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
                "requires stock game.exe 1.14d (1.14.3.71)")
        rig = self

        def bad_code(e):
            """IsBadCodePtr import: return the value this case set."""
            return rig.bad_code, 1
        emu.Emu.w_IsBadCodePtr = bad_code       # the import table is built in Emu(), so patch first
        self.m = emu.Emu(str(exe))
        self.arena = self.m.alloc(ARENA_SIZE)
        self.rng = random.Random(114)
        self.halts = []
        self.log = []
        self.outs = []
        self.custom = {}            # callee name -> function(emu) -> eax, replacing mix
        self.cases = 0
        self.bad_code = 0           # what the IsBadCodePtr import returns
        self.m.hook(GET_IP, lambda e: 0, 0)
        self.m.hook(ERR_HALT, self._halt, 0)
        self._callee("alloc", ALLOC, 3, ("ecx", "edx"), self._alloc)
        self._callee("free", FREE, 3, ("ecx", "edx"))

    def _halt(self, e):
        """0x40_8a60: note the line (third stack argument) and stop the run."""
        self.halts.append(e.arg(2))
        e.mu.emu_stop()
        return 0

    def _alloc(self, e):
        """Allocator stand-in: a fresh zeroed block, the same size the caller asked for."""
        return e.alloc(max(e.mu.reg_read(UC_X86_REG_EDX), 1))

    def _callee(self, name, addr, nstack, regs, fn=None, nlog=None):
        """Hook `addr`: log (name, registers, stack arguments) and return fn(e) or mix().

        `nlog` limits how many stack arguments are logged (the rest are popped, not compared:
        pointers to the caller's locals differ between the emulator and the model).
        """
        rig = self

        def hook(e):
            """Record the call and give its result."""
            regvals = tuple(e.mu.reg_read(r) for r in (
                {"ecx": UC_X86_REG_ECX, "edx": UC_X86_REG_EDX, "ebx": UC_X86_REG_EBX, "esi": UC_X86_REG_ESI,
                 "edi": UC_X86_REG_EDI, "eax": UC_X86_REG_EAX}[x] for x in regs))
            args = regvals + tuple(e.arg(i) for i in range(nstack if nlog is None else nlog))
            rig.log.append((name,) + args)
            handler = rig.custom.get(name) or fn
            out = handler(e) if handler else mix(name, *args)
            rig.outs.append(out & M32)
            return out
        self.m.hook(addr, hook, nstack)

    def callee(self, name, addr, nstack=0, regs=(), nlog=None):
        """Declare a hooked, unclaimed callee. `regs` lists the register arguments (ecx, edx, ebx, esi, edi, eax)."""
        self._callee(name, addr, nstack, regs, nlog=nlog)

    def slot(self, i, size=0x100):
        """The address of the i-th `size`-byte slot of the arena."""
        return self.arena + i * size

    def run(self, name, addr, build, model, nstack, mask=M32, ret=True):
        """Run `addr` ROUNDS times on random arenas against the model.

        build(img, rng) fills the arena image and returns a dict with any of
        stack (list), ecx, edx, regs (dict) and halts (not needed); model(m, a)
        gets the Model and that dict and returns the value for eax (or None).
        """
        for _ in range(ROUNDS):
            img = bytearray(self.rng.randbytes(ARENA_SIZE))
            args = build(img, self.rng)
            require(len(img) == ARENA_SIZE, f"{name}: the case builder wrote past the arena")
            self.m.mu.mem_write(self.arena, bytes(img))
            self.log.clear()
            self.outs.clear()
            self.halts.clear()
            stack = list(args.get("stack", []))
            where = f"{name} {addr:08x} {args}"
            eax = self.m.call(addr, *[s & M32 for s in stack], ecx=args.get("ecx", 0) & M32,
                              edx=args.get("edx", 0) & M32, regs=args.get("regs"), limit=LIMIT)
            esp = self.m.mu.reg_read(UC_X86_REG_ESP)
            want_sp = emu.STACK + emu.STACK_SIZE - 0x1000 - 4 * (len(stack) + 1) + 4 + 4 * nstack
            model_state = Model(self, bytearray(img), list(self.outs))
            halted = None
            result = None
            try:
                result = model(model_state, args)
            except Halt as h:
                halted = h.line
            if halted is not None:
                require(self.halts == [halted], f"{where}: halt {self.halts}, model line {halted:#x}")
            else:
                require(not self.halts, f"{where}: unexpected fatal error at line {[hex(x) for x in self.halts]}")
                require(esp == want_sp, f"{where}: ret popped {(esp - want_sp) // 4 + nstack} dwords, model {nstack}")
                if ret and result is not None:
                    require(eax & mask == result & mask, f"{where}: eax {eax & mask:#x}, model {result & mask:#x}")
            require(self.log == model_state.log, f"{where}: callee log {self.log}, model {model_state.log}")
            got = self.m.read(self.arena, ARENA_SIZE)
            want = bytes(model_state.regions[0][1])
            require(got == want, f"{where}: arena differs at {[hex(i) for i in range(ARENA_SIZE) if got[i] != want[i]][:8]}")
            for pointer, buf in model_state.blocks:
                require(self.m.read(pointer, len(buf)) == bytes(buf), f"{where}: allocated block {pointer:#x} differs")
            self.cases += 1
        return self.cases


def run_suite(suite, mutations, description, argv_mutation_check):
    """Run `suite(rig)` and print the ok line, or run it once per mutation and require each to fail.

    `suite` takes a Rig and returns the case count; the oracle's `set_mutation(name)` is
    the caller's closure over `mutations`, which is a dict name -> setter(bool).
    """
    if not argv_mutation_check:
        rig = Rig()
        count = suite(rig)
        print(f"ok: {count} {description} cases")
        return
    require(len(mutations) >= 3, "need at least 3 mutations")
    for name, setter in mutations.items():
        setter(True)
        try:
            suite(Rig())
        except Mismatch:
            print(f"mutation {name}: caught")
            continue
        finally:
            setter(False)
        raise SystemExit(f"mutation {name}: NOT caught")
    print(f"ok: {len(mutations)} mutations all caught")
