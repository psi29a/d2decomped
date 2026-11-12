# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe 1.14d's melee reach FUN_00622c40 and moving test FUN_00622dc0, run by their own code.

    uv run --project tools/emu python tools/emu/melee_reach.py

FUN_00480c80 (LAB_00480d06) ends a client's walk to a unit when
FUN_00622c40(player, target, FUN_00622dc0(target)) is true. Both run for
real here, with FUN_00463860 / FUN_00463900 (the target's MonStats BaseId,
from a small MonStats table set up here) too; hooked: FUN_00641530 (the
distance), FUN_00622870 (the unit's range), FUN_00622aa0 (the 0x804 line
test) and FUN_00621190 (a player's mode). d2d's rules::melee_reach and
rules::unit_moving (components/rules/melee_reach.hpp) are the Python
below; prints the cases that differ and how often the line test was asked
where d2d wouldn't ask it.
"""
import itertools
import sys

from emu import Emu

REACH, MOVING = 0x622C40, 0x622DC0
DISTANCE, RANGE, LINE, PLAYER_MODE = 0x641530, 0x622870, 0x622AA0, 0x621190
TABLES = 0x744304                 # its +0xa78 MonStats, +0xa80 their count
RECORD = 0x1A8                    # a MonStats row; BaseId the word at +2
VULTURE, TENTACLE, HEAD = 0x6E, 0x102, 0x105
CLASSES = (0, 1, VULTURE, VULTURE + 1, TENTACLE, TENTACLE + 1, HEAD, 0x200, 0x7FFF)   # 0x7fff: no row
BASES = {VULTURE + 1: VULTURE, TENTACLE + 1: TENTACLE}   # a row whose BaseId is another's


def unit_moving(unit_type, mode, base_id):
    if unit_type == 0:
        return mode in (2, 3, 6)
    if unit_type != 1:
        return False
    return mode in (2, 0xF) or (mode in (8, 9) and base_id == VULTURE)


def melee_reach(apart, unit_range, tentacle, extra, cut):
    if tentacle and unit_range + 8 > apart:
        return True
    if apart <= 0:
        return True
    return unit_range + extra + 1 >= apart and not cut


def main():
    emulator = Emu()
    rows = 0x300
    table = emulator.alloc(0xB00)
    monstats = emulator.alloc(rows * RECORD)
    emulator.w32(TABLES, table)
    emulator.w32(table + 0xA78, monstats)
    emulator.w32(table + 0xA80, rows)
    for row in range(rows):
        emulator.w32(monstats + row * RECORD, BASES.get(row, row) << 16)
    cur = {"apart": 0, "range": 0, "cut": 0, "mode": 0, "asked": 0}

    def line(_emu):
        cur["asked"] += 1
        return cur["cut"]

    emulator.hook(DISTANCE, lambda _emu: cur["apart"] & 0xFFFFFFFF, 2)
    emulator.hook(RANGE, lambda _emu: cur["range"], 1)
    emulator.hook(LINE, line, 3)
    emulator.hook(PLAYER_MODE, lambda _emu: cur["mode"] & 0xFFFFFFFF, 1)
    unit, target = emulator.alloc(0x200), emulator.alloc(0x200)
    emulator.w32(unit, 0)

    differ = total = extra_asks = 0
    for unit_type, row, mode in itertools.product((0, 1, 2, 4, 5), CLASSES, range(-1, 0x12)):
        emulator.w32(target, unit_type)
        emulator.w32(target + 4, row)
        emulator.w32(target + 0x10, mode)
        cur["mode"] = mode
        base = BASES.get(row, row) if row < rows else -1
        got = emulator.call(MOVING, target) & 0xFF
        want = unit_moving(unit_type, mode, base)
        total += 1
        if bool(got) != want:
            differ += 1
            print(f"moving: type {unit_type} class {row:x} mode {mode}: game.exe {got}, d2d {want}")
    for unit_type, row, apart, unit_range, extra, cut in itertools.product(
            (0, 1, 3), CLASSES, range(-2, 14), (0, 1, 2, 3), (0, 1), (0, 1)):
        emulator.w32(target, unit_type)
        emulator.w32(target + 4, row)
        cur.update(apart=apart, range=unit_range, cut=cut, asked=0)
        got = emulator.call(REACH, unit, target, extra)
        tentacle = unit_type == 1 and row < rows and BASES.get(row, row) in (TENTACLE, HEAD)
        want = melee_reach(apart, unit_range, tentacle, extra, cut)
        d2d_asks = not (tentacle and unit_range + 8 > apart) and apart > 0 and unit_range + extra + 1 >= apart
        extra_asks += cur["asked"] != int(d2d_asks)
        total += 1
        if bool(got) != want:
            differ += 1
            print(f"reach: type {unit_type} class {row:x} apart {apart} range {unit_range} extra {extra} cut {cut}: "
                  f"game.exe {got}, d2d {want}")
    print(f"{differ} of {total} differ; line test asked differently {extra_asks} times")
    sys.exit(1 if differ or extra_asks else 0)


if __name__ == "__main__":
    main()
