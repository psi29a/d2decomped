# SPDX-License-Identifier: GPL-3.0-or-later
"""Storm's SBig API wrappers on local game.exe 1.14d, against Python integers.

Run: uv run python sbig_api.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_00423790

Numbers are made and read through the cited SBigNew (FUN_00423950),
SBigFromBinary (FUN_00423850) and SBigToBinaryBuffer (FUN_00423b30), with
SMemAlloc, SMemFree and SMemReAlloc hooked as in source_cited.py. Each case
calls one wrapper on random values (up to 320 bits, 0 and 1 included) and
compares the result with Python's integer operation (decimal text, bytes
and a truncated string for the conversions), and requires the
wrapper to return normally within an instruction budget with its stack
arguments popped. `--mutation-check` plants defects in the model and
requires each to be caught. Checks game.exe only: d2d has no SRP or Warden
big numbers (source-sbig-api.md).
"""
import argparse
import hashlib
import random
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
NEW, FROM_BINARY, TO_BUFFER = 0x423950, 0x423850, 0x423B30
M32 = 0xFFFFFFFF


def decimal(text, base=10):
    """Each character adds its code minus '0' after a multiply by the base."""
    value = 0
    for char in text:
        value = value * base + char - 0x30
    return value


def width(a):
    """The dwords a value takes (at least one)."""
    return max(1, (a.bit_length() + 31) // 32)


OPS = {   # address: (arguments, operation, {mutation: operation})
    0x423790: ("ab", lambda a, b: a + b, {"add_plus_one": lambda a, b: a + b + 1, "add_xor": lambda a, b: a ^ b}),
}
POPS = {"ab": 3, "ab_ge": 3, "an": 3, "a": 2, "a1": 2, "u": 2, "test": 1}   # popped dwords, as in the RET


class Rig:
    def __init__(self, exe):
        self.machine = emu.Emu(exe)
        self.sizes = {}
        self.machine.hook(0x413020, self.allocate, 4)
        self.machine.hook(0x412650, lambda machine: 1, 4)
        self.machine.hook(0x4133D0, self.reallocate, 5)
        self.slot = self.machine.alloc(4)
        self.out = self.machine.alloc(0x100)
        self.used = self.machine.alloc(4)

    def allocate(self, machine):
        pointer = machine.alloc(max(machine.arg(0), 1))
        self.sizes[pointer] = machine.arg(0)
        return pointer

    def reallocate(self, machine):
        old, size = machine.arg(0), machine.arg(1)
        pointer = machine.alloc(max(size, 1))
        if old:
            machine.mu.mem_write(pointer, machine.read(old, min(size, self.sizes[old])))
        self.sizes[pointer] = size
        return pointer

    def run(self, address, *args, popped):
        machine = self.machine
        result = machine.call(address, *args, limit=5_000_000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise SystemExit(f"FUN_{address:08x} did not return within the budget")
        if machine.mu.reg_read(UC_X86_REG_ESP) != emu.STACK + emu.STACK_SIZE - 0x1000 + 4 * (popped - len(args)):
            raise SystemExit(f"FUN_{address:08x} popped the wrong number of arguments")
        return result

    def number(self, value):
        self.run(NEW, self.slot, popped=1)
        handle = self.machine.r32(self.slot)
        data = value.to_bytes(max(1, (value.bit_length() + 7) // 8), "little")
        self.run(FROM_BINARY, handle, self.machine.put(data), len(data), popped=3)
        return handle

    def value(self, handle):
        self.run(TO_BUFFER, handle, self.out, 0x100, self.used, popped=4)
        return int.from_bytes(self.machine.read(self.out, self.machine.r32(self.used)), "little")


def operand(rng):
    return rng.choice((0, 1, 2, M32, 1 << 32, (1 << 64) - 1, rng.getrandbits(rng.randint(1, 320))))


def compare(rig, seed, count, mutation=None, only=None):
    """Runs the cases on game.exe and the model; returns the first difference or None."""
    rng = random.Random(seed)
    for number in range(count):
        for address in sorted(OPS) if only is None else [only]:
            kind, operation, mutations = OPS[address]
            operation = mutations.get(mutation, operation)
            a, b, n = operand(rng), operand(rng), rng.choice((0, 1, 7, 31, 32, 33, 64, rng.randint(0, 200)))
            if kind == "ab_ge" and a < b:
                a, b = b, a
            if kind == "a1" and a == 0:
                a = 1
            ha = rig.number(a)
            if kind == "set":
                rig.run(address, ha, popped=1)
                got, want = rig.value(ha), operation(a)
            elif kind == "view":
                pointer, count = rig.machine.alloc(4), rng.choice((0, rig.machine.alloc(4)))
                rig.run(address, ha, pointer, count, popped=3)
                want = operation(a)
                got = rig.machine.read(rig.machine.r32(pointer), len(want))
                if count and rig.machine.r32(count) != len(want):
                    got = b"count %d" % rig.machine.r32(count)
                got, want = int.from_bytes(b"\1" + got, "little"), int.from_bytes(b"\1" + want, "little")
            elif kind == "tostr":
                size = rng.choice((1, 2, 5, rng.randint(1, 120), 120))
                buffer = rig.machine.put(b"\xcc" * 121)
                rig.run(address, ha, buffer, size, popped=3)
                text = operation(a, size)
                want = int.from_bytes(text + b"\0", "little")
                got = int.from_bytes(rig.machine.read(buffer, len(text) + 1), "little")
                n = size
            elif kind == "test":
                got, want = rig.run(address, ha, popped=1), operation(a)
            else:
                dest = rig.number(rng.choice((0, operand(rng))))
                if kind in ("ab", "ab_ge"):
                    rig.run(address, dest, ha, rig.number(b), popped=3)
                    want = operation(a, b)
                elif kind == "an":
                    rig.run(address, dest, ha, n, popped=3)
                    want = operation(a, n)
                elif kind == "n":
                    rig.run(address, dest, n, popped=2)
                    want = operation(n)
                elif kind == "str":
                    text = bytes(rng.choice(b"0123456789" * 6 + b"0:Az~") for _ in range(rng.randint(0, 40)))
                    rig.run(address, dest, rig.machine.put(text + b"\0"), popped=2)
                    want, a = operation(text), 0
                elif kind == "u":
                    u = rng.choice((0, 1, M32, rng.randint(0, M32)))
                    rig.run(address, dest, u, popped=2)
                    want, a = operation(u), u
                else:
                    rig.run(address, dest, ha, popped=2)
                    want = operation(a)
                got = rig.value(dest)
            if got != want:
                return f"case {number} FUN_{address:08x} a={a:#x} b={b:#x} n={n}: {got:#x} != {want:#x}"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", type=Path, help="game.exe 1.14d (default: bin/game.exe in the data folder emu.py finds)")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--count", type=int, default=200)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    exe = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit(f"{exe} is not game.exe 1.14d")
    rig = Rig(str(exe))
    difference = compare(rig, args.seed, args.count)
    if difference:
        raise SystemExit(f"FAIL {difference}")
    print(f"ok: {args.count} calls each of {len(OPS)} wrappers")
    if args.mutation_check:
        for address, (_, _, mutations) in OPS.items():
            for mutation in mutations:
                caught = compare(rig, args.seed, args.count, mutation, address)
                if not caught:
                    raise SystemExit(f"FAIL mutation {mutation} not caught")
                print(f"caught {mutation}: {caught[:140]}")


if __name__ == "__main__":
    main()
