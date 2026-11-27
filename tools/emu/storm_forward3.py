# SPDX-License-Identifier: GPL-3.0-or-later
"""The Storm compressed-sector forwarders on game.exe 1.14d, against a model.

Run: uv run python storm_forward3.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_0041e3a0, FUN_0041e400

Two byte-identical thiscall forwarders (30 bytes each) in the SCOMP.CPP
dispatch table at 0x6cfec8 and 0x6cfed0, one for each of the two packed-word
sizes the word-copy core `FUN_0041dce0` accepts. Each takes `thiscall` in ECX, a
pointer in EDX, and three stack args (a0, a1, a2); it dereferences EDX, calls
the core with this = thiscall, arg0 = a1, edx = *edx, stack[0] = *edx,
stack[1] = a0, stack[2] = its variant constant (1 or 2), writes the core's
return back into *edx, and propagates it unchanged (`RET 0xc`). `a2` is not
read, only popped.

The core is a real, complex word-copy routine, so this oracle hooks it as a
no-op and checks only the forwarding: the core is reached with the expected
args, the return is written back into the EDX pointer, the return value is
propagated, and the stack is clean. `--mutation-check` plants defects in the
model and requires each to be caught. Checks game.exe only: d2d has no
counterpart (storm-file.md).
"""
import argparse
import hashlib
import random
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CORE = 0x41DCE0
FORWARDERS = (
    (0x41E3A0, 1),
    (0x41E400, 2),
)
M32 = 0xFFFFFFFF
POPS = {addr: 0 for addr, _ in FORWARDERS}
HOOK_RET = 0


class Model:
    """The forwarders' expected core arguments, write-back and return propagation.
    The mutation is a planted defect in the model."""

    def __init__(self, mutation=None):
        self.mutation = mutation

    def run(self, thiscall, edx_val, a0, a1, a2, const):
        m = self.mutation
        want_ecx = thiscall
        want_eax = a1
        want_edx = edx_val
        want_s0 = edx_val
        want_s1 = a0
        want_s2 = const
        ret = HOOK_RET
        if m == "swap_this_a0":
            want_ecx, want_s1 = a0, thiscall
        elif m == "arg1_thiscall":
            want_eax = thiscall
        elif m == "edx_thiscall":
            want_edx = thiscall
            want_s0 = thiscall
        elif m == "const_thiscall":
            want_s2 = thiscall
        elif m == "ret_plus_one":
            ret = (HOOK_RET + 1) & M32
        return (want_ecx, want_eax, want_edx, want_s0, want_s1, want_s2, ret)


class Rig:
    def __init__(self, exe):
        self.machine = emu.Emu(exe)
        self.base = emu.STACK + emu.STACK_SIZE - 0x1000
        self.called = []
        # The core takes three stack args (cdecl) plus the thiscall in ECX and
        # a1 in EAX; the forwarder also pushes *edx and a0 as the first two
        # stack slots. The hook must pop the core's three stack args so the
        # forwarder's POP ESI / POP EBP / RET 0xc leaves the stack clean.
        self.machine.hook(CORE, self._on_core, 3)

    def _on_core(self, e):
        self.called.append(CORE)
        self.core_args = (
            e.mu.reg_read(UC_X86_REG_ECX),
            e.mu.reg_read(UC_X86_REG_EAX),
            e.mu.reg_read(UC_X86_REG_EDX),
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

    def run_forwarder(self, address, thiscall, edx_ptr, a0, a1, a2):
        self.called = []
        self.core_args = None
        result = self.run(address, a0, a1, a2, ecx=thiscall, edx=edx_ptr)
        if self.called != [CORE]:
            raise SystemExit(f"FUN_{address:08x} did not call the core: {self.called}")
        return result, self.core_args, edx_ptr


def compare(rig, seed, count, mutation=None):
    rng = random.Random(seed)
    model = Model(mutation)
    for address, const in FORWARDERS:
        for number in range(count):
            thiscall = rng.getrandbits(32)
            edx_val = rng.getrandbits(32)
            a0 = rng.getrandbits(32)
            a1 = rng.getrandbits(32)
            a2 = rng.getrandbits(32)
            edx_ptr = rig.machine.alloc(4)
            rig.machine.w32(edx_ptr, edx_val)
            got_ret, got_args, ptr = rig.run_forwarder(address, thiscall, edx_ptr, a0, a1, a2)
            got_edx_after = rig.machine.r32(ptr)
            want = model.run(thiscall, edx_val, a0, a1, a2, const)
            got = (got_ret, POPS[address], got_edx_after, got_args)
            want = (want[6], POPS[address], want[6], (want[0], want[1], want[2], want[3], want[4], want[5]))
            if got != want:
                return f"case {number} FUN_{address:08x} thiscall={thiscall:08x} edx_val={edx_val:08x} " \
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
    print(f"ok: {args.count} cases of the Storm compressed-sector forwarders")
    if args.mutation_check:
        for mutation in ("swap_this_a0", "arg1_thiscall", "edx_thiscall",
                        "const_thiscall", "ret_plus_one"):
            caught = compare(rig, args.seed, args.count, mutation)
            if not caught:
                raise SystemExit(f"FAIL mutation {mutation} not caught")
            print(f"caught {mutation}: {caught[:140]}")


if __name__ == "__main__":
    main()
