# SPDX-License-Identifier: GPL-3.0-or-later
"""The SBig reserve-digit helper on game.exe 1.14d, against a model.

Run: uv run python sbig_reserve_digit.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_00422050

A small `SOURCE` thiscall in the SBig API neighbourhood:
  * FUN_00422050: a thiscall that calls `sbig_reserve` (`FUN_00421fe0`) with
    the number in ECX and n on the stack, then returns the address of the
    digit at `+0x10` + n: `+8` + (`+0x10` + n) x 4. 33 bytes.

`sbig_reserve` is a real routine (grows the dword buffer and zero-fills it),
so this oracle hooks it as a no-op and checks only the forwarding and the
return: the reserve is reached with the number in ECX and n on the stack, the
return is `+8` + (`+0x10` + n) x 4 read from the number, and the stack is
clean. `--mutation-check` plants defects in the model and requires each to be
caught. Checks game.exe only: d2d has no SRP or Warden big numbers
(source-frontier.md).
"""
import argparse
import hashlib
import random
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
FORWARD = 0x422050
CORE = 0x421fe0
M32 = 0xFFFFFFFF
POPS = {FORWARD: 0}
HOOK_RET = 1  # scripted return of sbig_reserve; ignored by the forwarder


def word(rng):
    """A 32-bit value mixing edge cases (0, small, sign, max) and random."""
    return rng.choice((0, 1, 2, 0x7FFFFFFF, 0x80000000, M32, rng.randint(0, 0x40), rng.randint(0, M32)))


class Model:
    """The expected return of FUN_00422050 from the number's fields and n.
    The mutation is a planted defect in the model."""

    def __init__(self, mutation=None):
        self.mutation = mutation

    def run(self, buffer, first, n):
        m = self.mutation
        want_ret = (buffer + (first + n) * 4) & M32
        if m == "n_ignored":
            want_ret = (buffer + first * 4) & M32
        elif m == "n_plus_one":
            want_ret = (buffer + (first + n + 1) * 4) & M32
        elif m == "buffer_zero":
            want_ret = ((first + n) * 4) & M32
        elif m == "first_ignored":
            want_ret = (buffer + n * 4) & M32
        elif m == "stride_1":
            want_ret = (buffer + (first + n)) & M32
        elif m == "ret_zero":
            want_ret = 0
        return want_ret


class Rig:
    def __init__(self, exe):
        self.machine = emu.Emu(exe)
        self.base = emu.STACK + emu.STACK_SIZE - 0x1000
        self.called = []
        self.struct = self.machine.alloc(0x20)
        # sbig_reserve is thiscall (ECX = number, n on the stack, RET 4, one
        # stack arg). The hook pops that one arg so the forwarder's RET 4
        # leaves the stack clean.
        self.machine.hook(CORE, self._on_core, 1)

    def set_struct(self, buffer, first):
        """Write the number's buffer (+8) and first-digit index (+0x10)."""
        self.machine.w32(self.struct + 0x8, buffer)
        self.machine.w32(self.struct + 0x10, first)

    def _on_core(self, e):
        self.called.append(CORE)
        self.core_args = (e.mu.reg_read(UC_X86_REG_ECX), e.arg(0))
        return HOOK_RET

    def run(self, address, n, struct):
        machine = self.machine
        self.called = []
        self.core_args = None
        result = machine.call(address, n, ecx=struct, limit=100_000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise SystemExit(f"FUN_{address:08x} did not return within the budget")
        esp = machine.mu.reg_read(UC_X86_REG_ESP)
        expected = self.base - 4 * POPS[address]
        if esp != expected:
            raise SystemExit(
                f"FUN_{address:08x} left the stack dirty: {esp:08x} (want {expected:08x})")
        return result

    def run_forwarder(self, n):
        result = self.run(FORWARD, n, self.struct)
        if self.called != [CORE]:
            raise SystemExit(f"FUN_{FORWARD:08x} did not call the core: {self.called}")
        return result, self.core_args


def compare(rig, seed, count, mutation=None):
    rng = random.Random(seed)
    model = Model(mutation)
    for number in range(count):
        buffer = word(rng)
        first = word(rng)
        n = word(rng)
        rig.set_struct(buffer, first)
        got_ret, got_args = rig.run_forwarder(n)
        want_ret = model.run(buffer, first, n)
        want_args = (rig.struct, n)
        if got_ret != want_ret or got_args != want_args:
            return (f"case {number} FUN_{FORWARD:08x} struct={rig.struct:08x} "
                    f"buffer={buffer:08x} first={first:08x} n={n:08x}: "
                    f"ret {got_ret:08x} != {want_ret:08x}, args {got_args} != {want_args}")
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", type=Path, help="game.exe 1.14d")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--count", type=int, default=1000)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    if args.count <= 0:
        raise SystemExit("--count must be greater than 0")
    exe = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit(f"{exe} is not game.exe 1.14d")
    rig = Rig(str(exe))
    difference = compare(rig, args.seed, args.count)
    if difference:
        raise SystemExit(f"FAIL {difference}")
    print(f"ok: {args.count} cases of the SBig reserve-digit helper")
    if args.mutation_check:
        for mutation in ("n_ignored", "n_plus_one", "buffer_zero", "first_ignored", "stride_1", "ret_zero"):
            caught = compare(rig, args.seed, args.count, mutation)
            if not caught:
                raise SystemExit(f"FAIL mutation {mutation} not caught")
            print(f"caught {mutation}: {caught[:140]}")


if __name__ == "__main__":
    main()
