# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe 1.14d's critter neighbour test FUN_0046c600, run by its own code.

    uv run --project tools/emu python tools/emu/critter_neighbour.py

FUN_0046c570(ECX critter, EDX out, fn, best) walks the critter's rooms and
calls fn(ECX critter, EDX candidate, out, best). FUN_0046c600 keeps the
candidate when it isn't the critter, is a player (type 0) or a monster
(type 1) other than a chicken (0x95) or bunny (0x22c), FUN_00464820 is
false, and the distance (FUN_006416d0, hooked here) is below *best.
FUN_00464820 is called with ECX = the critter, so the candidate's own
death never matters. d2d's rules::critter_neighbour (components/rules/
critters.hpp) is that rule. Each case sets both units' type, class, mode
and +0xc4 flags, runs game.exe's function and compares; prints the count
that differ.
"""
import itertools
import sys

from emu import Emu

FUNCTION = 0x46c600
DISTANCE = 0x6416d0
CHICKEN, BUNNY = 0x95, 0x22c
TYPES = (0, 1, 2, 3, 4, 5)
CLASSES = (0, 2, 0x94, CHICKEN, 0x97, BUNNY)
MODES = (0, 1, 2, 0xc, 0x11)
FLAGS = (0, 0x10000)


def dead(unit_type, mode, flags):
    """FUN_00464820: the 0x10000 flag, or a player in mode 0 / 0x11, or a monster in mode 0 / 0xc."""
    if flags & 0x10000:
        return True
    if unit_type == 0:
        return mode in (0, 0x11)
    if unit_type == 1:
        return mode in (0, 0xc)
    return False


def critter_neighbour(critter_dead, unit_type, row):
    """d2d's rules::critter_neighbour."""
    if critter_dead:
        return False
    if unit_type == 0:
        return True
    return unit_type == 1 and row not in (CHICKEN, BUNNY)


def main():
    emulator = Emu()
    distance = [0]
    emulator.hook(DISTANCE, lambda _emu: distance[0], 2)
    critter, candidate = emulator.alloc(0x200), emulator.alloc(0x200)
    out, best = emulator.alloc(4), emulator.alloc(4)

    def setup(unit, unit_type, row, mode, flags):
        emulator.w32(unit, unit_type)
        emulator.w32(unit + 4, row)
        emulator.w32(unit + 0x10, mode)
        emulator.w32(unit + 0xc4, flags)

    def run(target, gap):
        distance[0] = gap
        emulator.w32(out, 0)
        emulator.w32(best, 10)
        emulator.call(FUNCTION, out, best, ecx=critter, edx=target)
        return emulator.r32(out) == target and emulator.r32(best) == gap

    differ = total = dead_kept = 0
    for critter_row, critter_mode, critter_flags in itertools.product((CHICKEN, BUNNY), MODES, FLAGS):
        setup(critter, 1, critter_row, critter_mode, critter_flags)
        critter_dead = dead(1, critter_mode, critter_flags)
        for unit_type, row, mode, flags, gap in itertools.product(TYPES, CLASSES, MODES, FLAGS, (3, 10)):
            setup(candidate, unit_type, row, mode, flags)
            got = run(candidate, gap)
            want = critter_neighbour(critter_dead, unit_type, row) and gap < 10
            total += 1
            dead_kept += got and dead(unit_type, mode, flags)
            if got != want:
                differ += 1
                print(f"critter mode {critter_mode:x} flags {critter_flags:x}, candidate type {unit_type} row {row:x} "
                      f"mode {mode:x} flags {flags:x} gap {gap}: game.exe {got}, d2d {want}")
        for target in (0, critter):   # no unit, the critter itself: never kept
            total += 1
            if run(target, 3):
                differ += 1
                print(f"candidate {target:x}: game.exe keeps it")
    print(f"{differ} of {total} differ; game.exe kept a dead candidate {dead_kept} times")
    sys.exit(1 if differ or not dead_kept else 0)


if __name__ == "__main__":
    main()
