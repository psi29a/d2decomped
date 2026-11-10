# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe 1.14d's unit flag test FUN_00451f30, run by its own code.

    uv run --project tools/emu python tools/emu/unit_flag_test.py [COUNT]

FUN_00451f30(ECX unit, EDX mask) is false for a null unit, else
`unit +0xc4 & mask != 0` (docs/research/re/cursor-units.md "Unit flag test").
Each case puts random flags at +0xc4 of a zeroed unit block, calls game.exe's
function and compares with that rule, plus the null unit and the masks the
callers pass (2, 4, 0x200, 0x80000000). Prints the count that differ.
"""
import random
import sys

from emu import Emu

FUNCTION = 0x451f30
FLAGS_AT = 0xc4
MASKS = (2, 4, 0x200, 0x80000000, 0, 0xffffffff)


def rule(flags, mask):
    return 1 if flags & mask else 0


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    emulator = Emu()
    unit = emulator.alloc(0x200)
    rng = random.Random(7)
    differ = 0
    total = 0
    if emulator.call(FUNCTION, ecx=0, edx=0xffffffff) & 0xff:
        differ += 1
        print("null unit: game.exe answers true")
    total += 1
    for case in range(count):
        flags = rng.getrandbits(32)
        mask = MASKS[case // 2 % len(MASKS)] if case % 2 else rng.getrandbits(32)
        emulator.w32(unit + FLAGS_AT, flags)
        got = emulator.call(FUNCTION, ecx=unit, edx=mask) & 0xff
        want = rule(flags, mask)
        total += 1
        if got != want:
            differ += 1
            print(f"flags {flags:08x} mask {mask:08x}: game.exe {got}, rule {want}")
    print(f"{differ} of {total} differ")
    sys.exit(1 if differ else 0)


if __name__ == "__main__":
    main()
