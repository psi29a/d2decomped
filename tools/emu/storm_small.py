# SPDX-License-Identifier: GPL-3.0-or-later
"""SErrSetBlizzardErrorFunction (FUN_0041c0e0) on game.exe 1.14d, against a model.

Run: uv run python storm_small.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_0041c0e0

The function stores its ECX argument (a function pointer) into the global
DAT_007790f8 and returns nothing (EAX is left at 0 by the emulator). Each
case gives a random function pointer, runs it on game.exe and on the model,
and compares the global and the return value. `--mutation-check` plants
defects in the model and requires each to be caught. Checks game.exe only:
d2d has no counterpart (source-small.md).
"""
import argparse
import hashlib
import random
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
M32 = 0xFFFFFFFF
ADDR = 0x41C0E0
GLOBAL = 0x7790F8
POPS = {ADDR: 1}


class Model:
    """SErrSetBlizzardErrorFunction: stores the ECX argument into the global."""

    def __init__(self, mutation=None):
        self.mutation = mutation
        self.global_ptr = 0

    def run(self, func):
        m = self.mutation
        if m == "plus_one":
            self.global_ptr = (func + 1) & M32
        elif m == "xor_dead":
            self.global_ptr = (func ^ 0xDEADBEEF) & M32
        elif m == "zero":
            self.global_ptr = 0
        else:
            self.global_ptr = func & M32
        return self.global_ptr, 0


class Rig:
    def __init__(self, exe):
        self.machine = emu.Emu(exe)

    def run(self, func):
        machine = self.machine
        result = machine.call(ADDR, ecx=func, limit=100_000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise SystemExit(f"FUN_{ADDR:08x} did not return within the budget")
        esp = machine.mu.reg_read(UC_X86_REG_ESP)
        if esp != emu.STACK + emu.STACK_SIZE - 0x1000:
            raise SystemExit(f"FUN_{ADDR:08x} left the stack dirty: {esp:08x}")
        global_ptr = machine.r32(GLOBAL)
        return global_ptr, result


def compare(rig, seed, count, mutation=None):
    rng = random.Random(seed)
    model = Model(mutation)
    for number in range(count):
        func = rng.getrandbits(32)
        got_global, got_result = rig.run(func)
        want_global, want_result = model.run(func)
        got = (got_global, got_result, POPS[ADDR])
        want = (want_global, want_result, POPS[ADDR])
        if got != want:
            return f"case {number} FUN_{ADDR:08x} func={func:08x}: {got} != {want}"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", type=Path, help="game.exe 1.14d")
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
    print(f"ok: {args.count} cases of SErrSetBlizzardErrorFunction")
    if args.mutation_check:
        for mutation in ("plus_one", "xor_dead", "zero"):
            caught = compare(rig, args.seed, args.count, mutation)
            if not caught:
                raise SystemExit(f"FAIL mutation {mutation} not caught")
            print(f"caught {mutation}: {caught[:140]}")


if __name__ == "__main__":
    main()
