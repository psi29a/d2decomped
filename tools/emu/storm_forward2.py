# SPDX-License-Identifier: GPL-3.0-or-later
"""The Storm compressed-sector forwarder on game.exe 1.14d, against a model.

Run: uv run python storm_forward2.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_0041e340

One small `SOURCE` function in the 0x41e3xx range:
  * FUN_0041e340: a thiscall forwarder that reorders its arguments before
    calling FUN_0041e250 (the compressed-sector unpacker core). It takes
    `thiscall` in ECX, `edx` in EDX, and three stack args (a0, a1, a2); it
    calls the core with this = a0, ESI = edx, and two stack args (original
    this, a1); the core pops those two (`RET 8`). The forwarder's third
    stack arg `a2` is not read, only popped (`RET 0xc`). It propagates the
    core's return value (it does not touch EAX). 24 bytes. The only in-image
    reference is a data pointer in the SCOMP.CPP dispatch table at 0x6cfee0.

The core is a real, complex function (security check, dereferences ESI),
so this oracle hooks it as a no-op and checks only the forwarding: the
core is reached, with the reordered arguments, the return value is
propagated, and the stack is clean. `--mutation-check` plants defects in
the model and requires each to be caught. Checks game.exe only: d2d has no
counterpart (storm-file.md).
"""
import argparse
import hashlib
import random
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
FORWARD = 0x41E340
CORE = 0x41E250
M32 = 0xFFFFFFFF
POPS = {FORWARD: 0}
HOOK_RET = 0


class Model:
    """The forwarder's expected core arguments and return propagation.
    The mutation is a planted defect in the model."""

    def __init__(self, mutation=None):
        self.mutation = mutation

    def run(self, thiscall, edx, a0, a1, a2):
        m = self.mutation
        want_this = a0
        want_esi = edx
        want_arg0 = thiscall
        want_arg1 = a1
        want_arg2 = 0  # dword above the core's two args: the forwarder's saved ESI (0 in the harness)
        ret = HOOK_RET
        if m == "swap_this_arg0":
            want_this, want_arg0 = thiscall, a0
        elif m == "arg1_thiscall":
            want_arg1 = thiscall
        elif m == "esi_edx_xor":
            want_esi = (edx ^ 0xDEADBEEF) & M32
        elif m == "arg2_thiscall":
            want_arg2 = thiscall
        elif m == "ret_plus_one":
            ret = (HOOK_RET + 1) & M32
        return (want_this, want_esi, want_arg0, want_arg1, want_arg2, ret)


class Rig:
    def __init__(self, exe):
        self.machine = emu.Emu(exe)
        self.base = emu.STACK + emu.STACK_SIZE - 0x1000
        self.called = []
        self.core_args = None
        # The wrapper pushes ESI, a1 and thiscall before the call and only
        # pops two (POP ESI, POP EBP) before RET 0xc, so the callee must
        # consume the return address plus two of those pushes.
        self.machine.hook(CORE, self._on_core, 2)

    def _on_core(self, e):
        self.called.append(CORE)
        self.core_args = (
            e.mu.reg_read(UC_X86_REG_ECX),
            e.mu.reg_read(UC_X86_REG_ESI),
            e.arg(0),
            e.arg(1),
            e.arg(2),
        )
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

    def run_forwarder(self, thiscall, edx, a0, a1, a2):
        self.called = []
        self.core_args = None
        result = self.run(FORWARD, a0, a1, a2, ecx=thiscall, edx=edx)
        if self.called != [CORE]:
            raise SystemExit(
                f"FUN_{FORWARD:08x} did not call the core: {self.called}")
        return result, self.core_args


def compare(rig, seed, count, mutation=None):
    rng = random.Random(seed)
    model = Model(mutation)
    for number in range(count):
        thiscall = rng.getrandbits(32)
        edx = rng.getrandbits(32)
        a0 = rng.getrandbits(32)
        a1 = rng.getrandbits(32)
        a2 = rng.getrandbits(32)
        got, got_args = rig.run_forwarder(thiscall, edx, a0, a1, a2)
        want = model.run(thiscall, edx, a0, a1, a2)
        got = (got, POPS[FORWARD], got_args)
        want = (want[5], POPS[FORWARD], (want[0], want[1], want[2], want[3], want[4]))
        if got != want:
            return f"case {number} FUN_{FORWARD:08x} thiscall={thiscall:08x} edx={edx:08x} " \
                   f"a0={a0:08x} a1={a1:08x}: {got} != {want}"
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
    print(f"ok: {args.count} cases of the Storm compressed-sector forwarder")
    if args.mutation_check:
        for mutation in ("swap_this_arg0", "arg1_thiscall", "esi_edx_xor",
                        "arg2_thiscall", "ret_plus_one"):
            caught = compare(rig, args.seed, args.count, mutation)
            if not caught:
                raise SystemExit(f"FAIL mutation {mutation} not caught")
            print(f"caught {mutation}: {caught[:140]}")


if __name__ == "__main__":
    main()
