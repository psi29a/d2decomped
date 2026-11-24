# SPDX-License-Identifier: GPL-3.0-or-later
"""Storm's bit reader and writer helpers on local game.exe 1.14d, against a model.

Run: uv run python storm_bits.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_0041d880 FUN_0041d8d0 FUN_0041d900

Each case gives one helper a record (`ECX`) with random fields, a random
source or destination buffer and random arguments, runs it on game.exe and
on the model, and compares the record, the buffer, the return value and the
stack arguments popped. `--mutation-check` plants defects in the model and
requires each to be caught. Checks game.exe only: d2d has no counterpart
(source-bits.md).
"""
import argparse
import hashlib
import random
import struct
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
M32 = 0xFFFFFFFF
SPAN = 64
# address: stack dwords popped (the RET)
POPS = {0x41D880: 1, 0x41D8D0: 2, 0x41D900: 1, 0x41D920: 2, 0x41D940: 0, 0x41D950: 2, 0x41D9A0: 0, 0x41DA30: 0}


class Model:
    """The helpers on a record (five dwords) and a byte buffer."""

    def __init__(self, mutation=None):
        self.mutation = mutation

    def run(self, address, rec, buf, base, args):
        m = self.mutation
        rec = list(rec)
        buf = bytearray(buf)
        result = None
        if address == 0x41D880:   # reader init: first dword, then the pointer past it
            src = args[0]
            rec[1] = struct.unpack_from("<I", buf, src - base)[0]
            rec[0] = (src + 4 + 4 * (m == "init_skip_8")) & M32
            rec[2] = 0x20
        if address == 0x41D8D0:   # peek: one 16-bit word in when the count is at most n
            n, mask = args
            if rec[2] <= n - (m == "peek_strict"):
                word = struct.unpack_from("<H", buf, rec[0] - base)[0]
                rec[1] = (rec[1] | (word << (rec[2] & 31))) & M32
                rec[2] = (rec[2] + 16) & M32
                rec[0] = (rec[0] + 2) & M32
            result = rec[1] & mask
        if address == 0x41D900:   # skip n bits
            n = args[0]
            rec[1] >>= (n & 31) + (m == "skip_one_more")
            rec[2] = (rec[2] - n) & M32
        return rec, bytes(buf), result


MUTATIONS = {
    0x41D880: ["init_skip_8"],
    0x41D8D0: ["peek_strict"],
    0x41D900: ["skip_one_more"],
}


class Rig:
    def __init__(self, exe):
        self.machine = emu.Emu(exe)
        self.record = self.machine.alloc(0x20)
        self.buffer = self.machine.alloc(SPAN)

    def run(self, address, rec, buf, args, regs):
        machine = self.machine
        machine.mu.mem_write(self.record, struct.pack("<5I", *rec))
        machine.mu.mem_write(self.buffer, buf)
        stack = [] if address == 0x41DA30 else list(args)
        result = machine.call(address, *stack, ecx=regs.get("ecx", self.record), limit=100_000,
                              regs={k: v for k, v in regs.items() if k != "ecx"})
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise SystemExit(f"FUN_{address:08x} did not return within the budget")
        popped = (machine.mu.reg_read(UC_X86_REG_ESP) - (emu.STACK + emu.STACK_SIZE - 0x1000) + 4 * len(stack)) // 4
        rec = list(struct.unpack("<5I", machine.read(self.record, 20)))
        return rec, machine.read(self.buffer, SPAN), result, popped


def case(rig, rng, address):
    base = rig.buffer
    buf = bytes(rng.randint(0, 255) for _ in range(SPAN))
    rec = [rng.getrandbits(32) for _ in range(5)]
    regs, args = {}, []
    if address == 0x41D880:
        args = [base + rng.randint(0, SPAN - 8)]
    elif address == 0x41D8D0:
        rec[0] = base + rng.randint(0, SPAN - 2)
        rec[2] = rng.randint(0, 32)
        rec[1] &= (1 << rec[2]) - 1 if rec[2] < 32 else M32
        args = [rng.randint(0, 32), rng.choice((M32, 0xFF, 0xFFFF, (1 << rng.randint(0, 32)) - 1))]
    elif address == 0x41D900:
        args = [rng.randint(0, 31)]
    elif address == 0x41D920:
        args = [rng.getrandbits(32), rng.choice((0, 1, rng.getrandbits(32)))]
    elif address == 0x41D940:
        pass
    elif address in (0x41D950, 0x41D9A0):
        rec[0] = base
        rec[2] = base + rng.randint(0, 8)
        rec[1] = rng.choice((0, 1, 2, rng.randint(0, 8)))
        rec[4] = rng.randint(0, 31) if address == 0x41D950 else rng.randint(0, 32)
        rec[3] &= (1 << rec[4]) - 1 if rec[4] < 32 else M32
        if address == 0x41D950:
            n = rng.randint(0, 32 - rec[4])
            args = [rng.getrandbits(n) if n else 0, n]
    elif address == 0x41DA30:
        level = rng.choice((0, 1, 2, 3, 4, rng.getrandbits(32)))
        args = [level]
        regs = {"edx": level, "ecx": base, "eax": base + 4}
    return rec, buf, args, regs


def compare(rig, seed, count, mutation=None, only=None):
    """Runs the cases on game.exe and the model; returns the first difference or None."""
    rng = random.Random(seed)
    model = Model(mutation)
    for number in range(count):
        for address in sorted(MUTATIONS) if only is None else [only]:
            rec, buf, args, regs = case(rig, rng, address)
            got_rec, got_buf, eax, popped = rig.run(address, rec, buf, args, regs)
            want_rec, want_buf, want_eax = model.run(address, rec, buf, rig.buffer, args)
            got = (got_rec, got_buf, eax if want_eax is not None else None, popped)
            want = (want_rec, want_buf, want_eax, POPS[address])
            if got != want:
                return f"case {number} FUN_{address:08x} args {args}: {got} != {want}"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", type=Path, help="game.exe 1.14d (default: bin/game.exe in the data folder emu.py finds)")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--count", type=int, default=1000)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    exe = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit(f"{exe} is not game.exe 1.14d")
    rig = Rig(str(exe))
    difference = compare(rig, args.seed, args.count)
    if difference:
        raise SystemExit(f"FAIL {difference}")
    print(f"ok: {args.count} cases each of {len(MUTATIONS)} helpers")
    if args.mutation_check:
        for address, mutations in MUTATIONS.items():
            for mutation in mutations:
                caught = compare(rig, args.seed, args.count, mutation, address)
                if not caught:
                    raise SystemExit(f"FAIL mutation {mutation} not caught")
                print(f"caught {mutation}: {caught[:140]}")


if __name__ == "__main__":
    main()
