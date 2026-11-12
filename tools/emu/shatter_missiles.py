# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe 1.14d's shattered death (FUN_004afc10), run by its own code.

    uv run python shatter_missiles.py

FUN_004afc10 (ESI the unit) is what the client's monster death (unit
command 8 in FUN_004aff60, LAB_004b05b9) calls when a state with the
States.txt shatter flag is on it (FUN_0063a6f0). Hooked here: the state
test FUN_00639df0(unit, state), the MonStats2 lookup FUN_00451fe0 (ECX the
class) and the client missile maker FUN_004cd540 (ECX its create struct),
which records the row (+0x10), the flags (+0), the owner (+4, +8) and +0x30.
Every unit type 0..3, MonStats2 Height 0..5 and 0xff, with freeze (1) and
uberminion (184) on or off, against d2d's rules::shatter_rows
(components/rules/shatter.hpp, copied below line for line). Prints `ok`.
"""
import itertools
import sys

from emu import Emu

FUNCTION = 0x4afc10
HAS_STATE = 0x639df0
MONSTATS2 = 0x451fe0
MAKE_MISSILE = 0x4cd540
FREEZE, UBERMINION = 1, 0xb8


def shatter_rows(unit_type, height, uberminion, frozen):
    """d2d's rules::shatter_rows."""
    if unit_type != 1:
        return []
    if uberminion and not frozen:
        return [0x1ab, 0x1aa]
    return [0x10f + (height & 0xff), 0x113]


def main():
    emulator = Emu()
    unit, record = emulator.alloc(0x200), emulator.alloc(0x134)
    states, made, bad = set(), [], []
    emulator.hook(HAS_STATE, lambda emu: int(emu.arg(1) in states), 2)
    emulator.hook(MONSTATS2, lambda _emu: record, 0)

    def make(emu):
        from unicorn.x86_const import UC_X86_REG_ECX
        create = emu.mu.reg_read(UC_X86_REG_ECX)
        if (emu.r32(create), emu.r32(create + 4), emu.r32(create + 8), emu.r32(create + 0x30)) != (0x2020, unit, unit, 1):
            bad.append(create)
        made.append(emu.r32(create + 0x10))
        return 0
    emulator.hook(MAKE_MISSILE, make, 0)
    differ = total = 0
    for unit_type, height, frozen, uber in itertools.product(range(4), (0, 1, 2, 3, 4, 5, 0xff), (False, True), (False, True)):
        emulator.w32(unit, unit_type)
        emulator.w32(unit + 4, 0x1d)
        emulator.mu.mem_write(record + 0xb, bytes((height,)))
        states.clear()
        states.update(state for state, on in ((FREEZE, frozen), (UBERMINION, uber)) if on)
        made.clear()
        emulator.call(FUNCTION, regs={"esi": unit})
        want = shatter_rows(unit_type, height, uber, frozen)
        total += 1
        if made != want:
            differ += 1
            print(f"type {unit_type} height {height} freeze {frozen} uberminion {uber}: game.exe {made}, d2d {want}")
    if differ or bad:
        sys.exit(f"{differ} of {total} differ; {len(bad)} create structs not 0x2020 from the unit")
    print(f"ok: {total} shattered deaths")


if __name__ == "__main__":
    main()
