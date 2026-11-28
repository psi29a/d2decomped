# SPDX-License-Identifier: GPL-3.0-or-later
"""The SBig from-bytes forwarder on game.exe 1.14d, against a model.

Run: uv run python sbig_from_bytes.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_00423890

One small `SOURCE` function in the SBig API range:
  * FUN_00423890: a stdcall forwarder (RET 0x10, 4 stack args a0..a3) that
    reorders its args and forwards to FUN_004234f0, the SBig count-reader/
    importer. It loads a3 ([EBP+0x14]) and a0 ([EBP+0x8]) and pushes a3, then
    a0, puts a1 ([EBP+0xc]) in EAX and a2 ([EBP+0x10]) in ECX, then calls
    FUN_004234f0. The callee sees stack[0] = a0, stack[1] = a3, EAX = a1,
    ECX = a2. It returns the callee's EAX unchanged and pops its 4 args
    (RET 0x10). No callers found in the image (XrefsTo: 0).

The callee FUN_004234f0 is a real, cited routine (reads a count via
FUN_00420f80 then imports bytes via FUN_00423380), so this oracle hooks it as
a no-op and checks only the forwarding: the callee is reached with the
reordered args (EAX = a1, ECX = a2, stack[0] = a0, stack[1] = a3), the return
value (random per case) is propagated, and the stack is clean. `--mutation-check` plants defects
in the model and requires each to be caught.

Checks game.exe only: d2d has no counterpart (source-sbig-api.md).
"""
import argparse
import hashlib
import random
from pathlib import Path

from unicorn.x86_const import (
    UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP,
)

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
FORWARD = 0x423890
CORE = 0x4234F0
M32 = 0xFFFFFFFF
POPS = {FORWARD: 0}


class Model:
    """The forwarder: the callee's EAX comes back in EAX unchanged. The
    mutation is a planted model bug."""

    def __init__(self, mutation=None):
        self.mutation = mutation

    def run(self, args, hook_ret):
        m = self.mutation
        if m == "plus_one":
            return (hook_ret + 1) & M32
        elif m == "xor_dead":
            return (hook_ret ^ 0xDEADBEEF) & M32
        elif m == "high_bit":
            return (hook_ret ^ 0x80000000) & M32
        else:
            return hook_ret & M32


class Rig:
    def __init__(self, exe):
        self.machine = emu.Emu(exe)
        self.base = emu.STACK + emu.STACK_SIZE - 0x1000
        self.called = []
        self.received = None
        self.hook_ret = 0
        # Hook the callee the forwarder calls so only the forwarding is checked.
        self.machine.hook(CORE, self._on_core, 2)

    def _on_core(self, e):
        """Record the args the callee received and return the scripted value."""
        self.called.append(CORE)
        self.received = {
            "eax": e.mu.reg_read(UC_X86_REG_EAX),
            "ecx": e.mu.reg_read(UC_X86_REG_ECX),
            "stack0": e.arg(0),
            "stack1": e.arg(1),
        }
        return self.hook_ret

    def run(self, address, a0, a1, a2, a3):
        """Call the forwarder with 4 stack args and require normal return and
        the expected stack cleanup (base minus 4 * POPS[address] bytes)."""
        machine = self.machine
        result = machine.call(address, a0, a1, a2, a3, limit=100_000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise SystemExit(f"FUN_{address:08x} did not return within the budget")
        esp = machine.mu.reg_read(UC_X86_REG_ESP)
        expected = self.base - 4 * POPS[address]
        if esp != expected:
            raise SystemExit(
                f"FUN_{address:08x} left the stack dirty: {esp:08x} "
                f"(want {expected:08x})")
        return result

    def run_forwarder(self, a0, a1, a2, a3, hook_ret):
        """Call the forwarder and verify the callee was reached with the
        reordered args."""
        self.called = []
        self.received = None
        self.hook_ret = hook_ret
        result = self.run(FORWARD, a0, a1, a2, a3)
        if self.called != [CORE]:
            raise SystemExit(
                f"FUN_{FORWARD:08x} did not call FUN_{CORE:08x}: {self.called}")
        r = self.received
        if r["eax"] != a1:
            raise SystemExit(
                f"FUN_{FORWARD:08x} passed eax={r['eax']:08x} to the callee, "
                f"want a1={a1:08x}")
        if r["ecx"] != a2:
            raise SystemExit(
                f"FUN_{FORWARD:08x} passed ecx={r['ecx']:08x} to the callee, "
                f"want a2={a2:08x}")
        if r["stack0"] != a0:
            raise SystemExit(
                f"FUN_{FORWARD:08x} passed stack0={r['stack0']:08x} to the "
                f"callee, want a0={a0:08x}")
        if r["stack1"] != a3:
            raise SystemExit(
                f"FUN_{FORWARD:08x} passed stack1={r['stack1']:08x} to the "
                f"callee, want a3={a3:08x}")
        return result


def compare(rig, seed, count, mutation=None):
    rng = random.Random(seed)
    model = Model(mutation)
    for number in range(count):
        a0 = rng.getrandbits(32)
        a1 = rng.getrandbits(32)
        a2 = rng.getrandbits(32)
        a3 = rng.getrandbits(32)
        hook_ret = rng.getrandbits(32)
        got = rig.run_forwarder(a0, a1, a2, a3, hook_ret)
        want = model.run((a0, a1, a2, a3), hook_ret)
        got = (got, POPS[FORWARD])
        want = (want, POPS[FORWARD])
        if got != want:
            return (
                f"case {number} FUN_{FORWARD:08x} "
                f"a0={a0:08x} a1={a1:08x} a2={a2:08x} a3={a3:08x}: "
                f"{got} != {want}")
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
    print(f"ok: {args.count} cases of the SBig from-bytes forwarder")
    if args.mutation_check:
        for mutation in ("plus_one", "xor_dead", "high_bit"):
            caught = compare(rig, args.seed, args.count, mutation)
            if not caught:
                raise SystemExit(f"FAIL mutation {mutation} not caught")
            print(f"caught {mutation}: {caught[:140]}")


if __name__ == "__main__":
    main()
