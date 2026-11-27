# SPDX-License-Identifier: GPL-3.0-or-later
"""The SBigNew forwarder on game.exe 1.14d, against a model.

Run: uv run python sbig_new_forward.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_0041d320

A small `SOURCE` thiscall in the SBig API neighbourhood:
  * FUN_0041d320: a thiscall forwarder to `SBigNew` (`FUN_00423950`). It takes
    the output handle pointer in ECX, pushes it as the sole stack argument of
    the stdcall constructor, discards the constructor's return value, and
    returns the ECX handle unchanged in EAX (`RET 0`). 13 bytes.

The constructor is a real, complex routine (allocates 0x168 bytes, builds 16
temporaries, writes the result into the output pointer), so this oracle hooks
it as a no-op and checks only the forwarding: the constructor is reached with
the ECX handle as its single stack argument, the return value is that handle,
and the stack is clean. `--mutation-check` plants defects in the model and
requires each to be caught. Checks game.exe only: d2d has no SRP/Warden big
numbers (source-sbig-api.md).
"""
import argparse
import hashlib
import random
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
FORWARD = 0x41D320
CORE = 0x423950
M32 = 0xFFFFFFFF
POPS = {FORWARD: 0}
HOOK_RET = 1  # the constructor's scripted return value (success)


class Model:
    """The forwarder's expected core argument and return propagation.
    The mutation is a planted defect in the model."""

    def __init__(self, mutation=None):
        self.mutation = mutation

    def run(self, thiscall):
        m = self.mutation
        want_arg0 = thiscall
        want_ret = thiscall
        if m == "arg_zero":
            want_arg0 = 0
        elif m == "arg_xor":
            want_arg0 = (thiscall ^ 0xDEADBEEF) & M32
        elif m == "ret_zero":
            want_ret = 0
        elif m == "ret_sbig_ret":
            want_ret = HOOK_RET
        return (want_arg0, want_ret)


class Rig:
    def __init__(self, exe):
        self.machine = emu.Emu(exe)
        self.base = emu.STACK + emu.STACK_SIZE - 0x1000
        self.called = []
        # The constructor takes one stack arg (cdecl, RET 0x4) plus no registers;
        # the forwarder pushes ECX as that single slot. The hook pops it so the
        # forwarder's POP ESI / RET leaves the stack clean.
        self.machine.hook(CORE, self._on_core, 1)

    def _on_core(self, e):
        self.called.append(CORE)
        self.core_args = (e.arg(0),)
        return HOOK_RET

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

    def run_forwarder(self, thiscall):
        self.called = []
        self.core_args = None
        result = self.run(FORWARD, ecx=thiscall)
        if self.called != [CORE]:
            raise SystemExit(f"FUN_{FORWARD:08x} did not call the core: {self.called}")
        return result, self.core_args


def compare(rig, seed, count, mutation=None):
    rng = random.Random(seed)
    model = Model(mutation)
    for number in range(count):
        thiscall = rng.getrandbits(32)
        got_ret, got_args = rig.run_forwarder(thiscall)
        want = model.run(thiscall)
        got = (got_ret, POPS[FORWARD], got_args)
        want = (want[1], POPS[FORWARD], (want[0],))
        if got != want:
            return f"case {number} FUN_{FORWARD:08x} thiscall={thiscall:08x}: {got} != {want}"
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
    print(f"ok: {args.count} cases of the SBigNew forwarder")
    if args.mutation_check:
        for mutation in ("arg_zero", "arg_xor", "ret_zero", "ret_sbig_ret"):
            caught = compare(rig, args.seed, args.count, mutation)
            if not caught:
                raise SystemExit(f"FAIL mutation {mutation} not caught")
            print(f"caught {mutation}: {caught[:140]}")


if __name__ == "__main__":
    main()
