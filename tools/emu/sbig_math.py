# SPDX-License-Identifier: GPL-3.0-or-later
"""Storm's SBig arithmetic wrappers on local game.exe 1.14d, against Python integers.

Run: uv run python sbig_math.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_00424e70 FUN_00424eb0 FUN_00424ed0 FUN_00424f10 FUN_00424f30

Numbers are made and read through the cited SBigNew (FUN_00423950),
SBigFromBinary (FUN_00423850) and SBigToBinaryBuffer (FUN_00423b30), with
SMemAlloc, SMemFree and SMemReAlloc hooked as in source_cited.py. Each case
calls one wrapper on random values (up to 320 bits, 0 and 1 included) and
compares the result with Python's integer operation (divisors and moduli
non-zero; three pairs in ten share a factor), and requires the
wrapper to return normally within an instruction budget with its stack
arguments popped. `--mutation-check` plants defects in the model and
requires each to be caught. Checks game.exe only: d2d has no SRP or Warden
big numbers (source-sbig-math.md).
"""
import argparse
import hashlib
import math
import random
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
NEW, FROM_BINARY, TO_BUFFER = 0x423950, 0x423850, 0x423B30
M32 = 0xFFFFFFFF


OPS = {   # address: (arguments, operation, {mutation: operation})
    0x424E70: ("ab", lambda a, b: a // b, {"div_rounds_up": lambda a, b: -(-a // b)}),
    0x424EB0: ("ab0", math.gcd, {"gcd_min": lambda a, b: min(a, b) if a and b else a or b}),
    0x424ED0: ("ab", lambda a, b: a % b, {"mod_quotient": lambda a, b: a // b}),
    0x424F10: ("ab0", lambda a, b: a * b, {"mul_plus_a": lambda a, b: a * b + a}),
    0x424F30: ("abc", lambda a, b, c: a * b % c, {"mul_mod_no_reduce": lambda a, b, c: a * b}),
}


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
            a, b, c = operand(rng), operand(rng), operand(rng)
            if rng.random() < 0.3:   # shared factors for the gcd
                factor = operand(rng) | 1
                a, b = a * factor, b * factor
            if kind in ("ab", "abc"):
                b = b or 1
                c = c or 1
            dest = rig.number(rng.choice((0, operand(rng))))
            if kind == "a":
                rig.run(address, dest, rig.number(a), popped=2)
                want = operation(a)
            elif kind == "abc":
                rig.run(address, dest, rig.number(a), rig.number(b), rig.number(c), popped=4)
                want = operation(a, b, c)
            else:
                rig.run(address, dest, rig.number(a), rig.number(b), popped=3)
                want = operation(a, b)
            got = rig.value(dest)
            if got != want:
                return f"case {number} FUN_{address:08x} a={a:#x} b={b:#x} c={c:#x}: {got:#x} != {want:#x}"
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
