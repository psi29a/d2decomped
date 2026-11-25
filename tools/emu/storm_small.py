# SPDX-License-Identifier: GPL-3.0-or-later
"""Small Storm functions on game.exe 1.14d, against a model.

Run: uv run python storm_small.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_0041c0e0 FUN_0041c0d0

Two tiny SOURCE functions in the 0x41c0xx range:
  * FUN_0041c0e0 SErrSetBlizzardErrorFunction: stores its ECX argument
    (a function pointer) into DAT_007790f8, returns nothing.
  * FUN_0041c0d0 storm_strpbrk: tail-calls the MSVC CRT
    _strpbrk(str, charset), returning the pointer to the first character of
    str that is in charset, or 0.

Each case runs the function on game.exe and on the model and compares the
observable state (the global / the returned pointer). `--mutation-check`
plants defects in the model and requires each to be caught. Checks game.exe
only: d2d has no counterpart (source-small.md).
"""
import argparse
import hashlib
import random
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
M32 = 0xFFFFFFFF
SETTER = 0x41C0E0
STRPBRK = 0x41C0D0
GLOBAL = 0x7790F8
POPS = {SETTER: 0, STRPBRK: 2}


def strpbrk_offset(str_data, charset, mutation):
    """C _strpbrk: first char of str (NUL-terminated) that is in charset,
    as an offset into str, or None. The mutation is a planted model bug."""
    cs = set()
    for b in charset:
        if b == 0:
            break
        cs.add(b)
    # strpbrk_empty_charset: an empty charset matches everything (bug).
    if mutation == "strpbrk_empty_charset" and not cs:
        cs = None
    for i, b in enumerate(str_data):
        if b == 0:
            break
        if mutation == "strpbrk_first":
            # bug: ignore the charset, return the first character.
            return i
        if mutation == "strpbrk_strict":
            # bug: only the last character can match.
            if i == len(str_data) - 1 and b in cs:
                return i
            continue
        if cs is None or b in cs:
            return i
    return None


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
        self.base = emu.STACK + emu.STACK_SIZE - 0x1000

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

    def run_setter(self, func):
        result = self.run(SETTER, ecx=func)
        return self.machine.r32(GLOBAL), result

    def run_strpbrk(self, str_ptr, charset_ptr):
        return self.run(STRPBRK, str_ptr, charset_ptr)


def compare(rig, seed, count, mutation=None):
    rng = random.Random(seed)
    model = Model(mutation)
    for number in range(count):
        # T1: setter
        func = rng.getrandbits(32)
        got_global, got_result = rig.run_setter(func)
        want_global, want_result = model.run(func)
        got = (got_global, got_result, POPS[SETTER])
        want = (want_global, want_result, POPS[SETTER])
        if got != want:
            return f"case {number} FUN_{SETTER:08x} func={func:08x}: {got} != {want}"
        # T2: _strpbrk forwarder
        str_len = rng.choice((0, 1, 4, 16, 64))
        str_data = bytes(rng.choice(range(1, 128)) for _ in range(str_len))
        charset_len = rng.choice((0, 1, 3, 8))
        charset_data = bytes(rng.choice(range(1, 128)) for _ in range(charset_len))
        str_buf = rig.machine.put(str_data)
        charset_buf = rig.machine.put(charset_data)
        got_ptr = rig.run_strpbrk(str_buf, charset_buf)
        want_offset = strpbrk_offset(str_data, charset_data, mutation)
        want_ptr = 0 if want_offset is None else str_buf + want_offset
        got = (got_ptr, POPS[STRPBRK])
        want = (want_ptr, POPS[STRPBRK])
        if got != want:
            return (f"case {number} FUN_{STRPBRK:08x} str={str_data!r} "
                    f"charset={charset_data!r}: {got} != {want}")
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
    print(f"ok: {args.count} cases of both small Storm functions")
    if args.mutation_check:
        for mutation in ("plus_one", "xor_dead", "zero",
                         "strpbrk_first", "strpbrk_strict", "strpbrk_empty_charset"):
            caught = compare(rig, args.seed, args.count, mutation)
            if not caught:
                raise SystemExit(f"FAIL mutation {mutation} not caught")
            print(f"caught {mutation}: {caught[:140]}")


if __name__ == "__main__":
    main()
