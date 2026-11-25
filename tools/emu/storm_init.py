# SPDX-License-Identifier: GPL-3.0-or-later
"""The Storm init on game.exe 1.14d, against a model.

Run: uv run python storm_init.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_0041bee0

One small `SOURCE` function in the 0x41bee0 range:
  * FUN_0041bee0: a no-argument void init that calls three functions in a
    fixed order and then returns. `CALL 0x00411cd0` (memory-protection
    critical sections), `CALL 0x00420040` (SLog critical sections), then
    `JMP 0x00414360` (power-of-10 table plus `DAT_007788dc = 1`). 15 bytes.
    Callers: `FUN_0041c0c0` (`storm_init_forward`) and a 5-byte
    `JMP` trampoline at 0x6ca890.

The three callees are each their own concern (memory-protection sections,
SLog sections, and the power-of-10 table), so this oracle hooks each as a
no-op and checks only the init's shape: the three callees are reached, in
order, and the function returns normally with a clean stack.
`--mutation-check` plants defects in the expected call sequence and requires
each to be caught. Checks game.exe only: d2d has no counterpart
(source-storm-init.md).
"""
import argparse
import hashlib
import random
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
INIT = 0x41BEE0
MEM_SECTIONS = 0x411CD0
SLOG_SECTIONS = 0x420040
FINISH = 0x414360
POPS = {INIT: 0}
EXPECTED = (MEM_SECTIONS, SLOG_SECTIONS, FINISH)


class Model:
    """The expected call sequence of the init. The mutation is a planted
    defect in that sequence."""

    def __init__(self, mutation=None):
        self.mutation = mutation

    def run(self):
        m = self.mutation
        if m == "wrong_order":
            return (SLOG_SECTIONS, MEM_SECTIONS, FINISH)
        elif m == "missing_call":
            return (MEM_SECTIONS, SLOG_SECTIONS)
        elif m == "extra_call":
            return (MEM_SECTIONS, SLOG_SECTIONS, FINISH, FINISH)
        else:
            return EXPECTED


class Rig:
    def __init__(self, exe):
        self.machine = emu.Emu(exe)
        self.base = emu.STACK + emu.STACK_SIZE - 0x1000
        self.call_order = []
        # Hook the three callees as no-ops that record the call order.
        for address in EXPECTED:
            self.machine.hook(address, self._hook(address), 0)

    def _hook(self, address):
        def handler(e):
            self.call_order.append(address)
            return 0
        return handler

    def run(self, address, *args, ecx=0):
        """Call a function and require normal return and the expected stack
        cleanup (base minus 4 * POPS[address] bytes)."""
        machine = self.machine
        self.call_order = []
        result = machine.call(address, *args, ecx=ecx, limit=100_000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise SystemExit(f"FUN_{address:08x} did not return within the budget")
        esp = machine.mu.reg_read(UC_X86_REG_ESP)
        expected = self.base - 4 * POPS[address]
        if esp != expected:
            raise SystemExit(f"FUN_{address:08x} left the stack dirty: {esp:08x} (want {expected:08x})")
        return result, list(self.call_order)

    def run_init(self):
        return self.run(INIT)


def compare(rig, seed, count, mutation=None):
    rng = random.Random(seed)
    model = Model(mutation)
    for number in range(count):
        rng.getrandbits(32)  # keep the seed exercised; the init takes no argument
        got_result, got_order = rig.run_init()
        want = model.run()
        got = (got_result, POPS[INIT], got_order)
        want = (0, POPS[INIT], list(want))
        if got != want:
            return f"case {number} FUN_{INIT:08x}: {got} != {want}"
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
    print(f"ok: {args.count} cases of the Storm init")
    if args.mutation_check:
        for mutation in ("wrong_order", "missing_call", "extra_call"):
            caught = compare(rig, args.seed, args.count, mutation)
            if not caught:
                raise SystemExit(f"FAIL mutation {mutation} not caught")
            print(f"caught {mutation}: {caught[:140]}")


if __name__ == "__main__":
    main()
