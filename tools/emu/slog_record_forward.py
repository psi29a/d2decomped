# SPDX-License-Identifier: GPL-3.0-or-later
"""The SLog record initialiser forwarder on game.exe 1.14d, against a model.

Run: uv run python slog_record_forward.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_0041f920

One small `SOURCE` function in the SLog range:
  * FUN_0041f920: saves its ECX argument (a record pointer) in ESI, calls the
    record initialiser FUN_0041f4a0, and returns the saved argument unchanged
    in EAX. A one-argument fastcall forwarder (PUSH ESI / MOV ESI,ECX /
    CALL 0x0041f4a0 / MOV EAX,ESI / POP ESI / RET, 12 bytes). No callers found
    in the image (XrefsTo: 0).

The initialiser itself (its list fields and the CRT constructor at 0x6869fc)
is a separate concern, so this oracle hooks FUN_0041f4a0 as a no-op and checks
only the forwarding: the argument comes back in EAX and the stack is clean.
`--mutation-check` plants defects in the model and requires each to be caught.
Checks game.exe only: d2d has no counterpart (source-slog-record.md).
"""
import argparse
import hashlib
import random
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
FORWARD = 0x41F920
RECORD = 0x41F4A0
M32 = 0xFFFFFFFF
POPS = {FORWARD: 0}


class Model:
    """The forwarder: the ECX argument comes back in EAX unchanged.
    The mutation is a planted model bug."""

    def __init__(self, mutation=None):
        self.mutation = mutation

    def run(self, func):
        m = self.mutation
        if m == "plus_one":
            return (func + 1) & M32
        elif m == "xor_dead":
            return (func ^ 0xDEADBEEF) & M32
        elif m == "zero":
            return 0
        else:
            return func & M32


class Rig:
    def __init__(self, exe):
        self.machine = emu.Emu(exe)
        self.base = emu.STACK + emu.STACK_SIZE - 0x1000
        self.called = []
        # Hook the record initialiser the forwarder calls so only the
        # forwarding is checked.
        self.machine.hook(RECORD, self._on_record, 0)

    def _on_record(self, e):
        self.called.append(RECORD)
        return 0

    def run(self, address, *args, ecx=0):
        """Call a function and require normal return and the expected stack
        cleanup (base minus 4 * POPS[address] bytes)."""
        machine = self.machine
        result = machine.call(address, *args, ecx=ecx, limit=100_000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise SystemExit(f"FUN_{address:08x} did not return within the budget")
        esp = machine.mu.reg_read(UC_X86_REG_ESP)
        expected = self.base - 4 * POPS[address]
        if esp != expected:
            raise SystemExit(f"FUN_{address:08x} left the stack dirty: {esp:08x} (want {expected:08x})")
        return result

    def run_forwarder(self, value):
        self.called = []
        result = self.run(FORWARD, ecx=value)
        if self.called != [RECORD]:
            raise SystemExit(
                f"FUN_{FORWARD:08x} did not call FUN_{RECORD:08x}: {self.called}")
        return result


def compare(rig, seed, count, mutation=None):
    rng = random.Random(seed)
    model = Model(mutation)
    for number in range(count):
        value = rng.getrandbits(32)
        got = rig.run_forwarder(value)
        want = model.run(value)
        got = (got, POPS[FORWARD])
        want = (want, POPS[FORWARD])
        if got != want:
            return f"case {number} FUN_{FORWARD:08x} value={value:08x}: {got} != {want}"
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
    print(f"ok: {args.count} cases of the SLog record initialiser forwarder")
    if args.mutation_check:
        for mutation in ("plus_one", "xor_dead", "zero"):
            caught = compare(rig, args.seed, args.count, mutation)
            if not caught:
                raise SystemExit(f"FAIL mutation {mutation} not caught")
            print(f"caught {mutation}: {caught[:140]}")


if __name__ == "__main__":
    main()
