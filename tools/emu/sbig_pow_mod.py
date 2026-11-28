# SPDX-License-Identifier: GPL-3.0-or-later
"""The SBig pow-mod forwarder on game.exe 1.14d, against a model.

Run: uv run python sbig_pow_mod.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_004255c0

A small `SOURCE` stdcall in the SBig math neighbourhood:
  * FUN_004255c0: a stdcall forwarder to `sbig_pow_mod` (`FUN_00425350`). It
    takes one stack argument (a number pointer), adds 0x14 to it, pushes the
    result as the sole stack argument of the modular-exponentiation core, and
    returns the core's EAX unchanged (`RET 0x4`). 21 bytes.

The core is a real, complex routine (2-bit-window modular exponentiation over
the `+0x140` EAX pool, squarings and multiply-reduces via `FUN_00424b00` and
`FUN_00423dc0`), so this oracle hooks it as a no-op and checks only the
forwarding: the core is reached with the pointer advanced by 0x14, the return
value is propagated, and the stack is clean. `--mutation-check` plants defects
in the model and requires each to be caught. Checks game.exe only: d2d has no
SRP/Warden big numbers (source-frontier.md).
"""
import argparse
import hashlib
import random
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
FORWARD = 0x4255C0
CORE = 0x425350
M32 = 0xFFFFFFFF
POPS = {FORWARD: 0}


class Model:
    """The forwarder's expected core argument and return propagation.
    The mutation is a planted defect in the model."""

    def __init__(self, mutation=None):
        self.mutation = mutation

    def run(self, arg0, hook_ret):
        m = self.mutation
        want_arg0 = (arg0 + 0x14) & M32
        want_ret = hook_ret
        if m == "arg_zero":
            want_arg0 = 0
        elif m == "arg_xor":
            want_arg0 = (arg0 ^ 0xDEADBEEF) & M32
        elif m == "arg_offset":
            want_arg0 = (arg0 + 0x10) & M32
        elif m == "ret_zero":
            want_ret = 0
        elif m == "ret_plus_one":
            want_ret = (hook_ret + 1) & M32
        return (want_arg0, want_ret)


class Rig:
    def __init__(self, exe):
        self.machine = emu.Emu(exe)
        self.base = emu.STACK + emu.STACK_SIZE - 0x1000
        self.called = []
        self.hook_ret = 0  # the core's scripted return value, random per case
        # The core takes one stack arg (cdecl, RET 0x4); the forwarder pushes
        # (arg0 + 0x14) as that single slot. The hook pops it so the forwarder's
        # POP EBX / POP EBP / RET 0x4 leaves the stack clean.
        self.machine.hook(CORE, self._on_core, 1)

    def _on_core(self, e):
        self.called.append(CORE)
        self.core_args = (e.arg(0),)
        return self.hook_ret

    def run(self, address, *args, ecx=0, edx=0):
        machine = self.machine
        result = machine.call(address, *args, ecx=ecx, edx=edx, limit=100_000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise SystemExit(f"FUN_{address:08x} did not return within the budget")
        esp = machine.mu.reg_read(UC_X86_REG_ESP)
        expected = self.base - 4 * POPS[address]
        if esp != expected:
            raise SystemExit(
                f"FUN_{address:08x} left the stack dirty: {esp:08x} (want {expected:08x})")
        return result

    def run_forwarder(self, arg0, hook_ret):
        self.called = []
        self.core_args = None
        self.hook_ret = hook_ret
        result = self.run(FORWARD, arg0)
        if self.called != [CORE]:
            raise SystemExit(f"FUN_{FORWARD:08x} did not call the core: {self.called}")
        return result, self.core_args


def compare(rig, seed, count, mutation=None):
    rng = random.Random(seed)
    model = Model(mutation)
    for number in range(count):
        arg0 = rng.getrandbits(32)
        hook_ret = rng.getrandbits(32)
        got_ret, got_args = rig.run_forwarder(arg0, hook_ret)
        want = model.run(arg0, hook_ret)
        got = (got_ret, POPS[FORWARD], got_args)
        want = (want[1], POPS[FORWARD], (want[0],))
        if got != want:
            return f"case {number} FUN_{FORWARD:08x} arg0={arg0:08x}: {got} != {want}"
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
    print(f"ok: {args.count} cases of the SBig pow-mod forwarder")
    if args.mutation_check:
        for mutation in ("arg_zero", "arg_xor", "arg_offset", "ret_zero", "ret_plus_one"):
            caught = compare(rig, args.seed, args.count, mutation)
            if not caught:
                raise SystemExit(f"FAIL mutation {mutation} not caught")
            print(f"caught {mutation}: {caught[:140]}")


if __name__ == "__main__":
    main()
