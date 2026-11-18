# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the client-unit seed made by UNIT\\CUnit.cpp FUN_00465fd0.

    .venv/bin/python object_client_seed.py [--mutation-check]

Runs stock game.exe on synthetic unit and room1 records. The type-5 setup
callees are hooked at their subsystem boundaries; FUN_00465fd0 itself and the
two seed setters run for real. The check proves that a positioned unit advances
room1 +0x6c/+0x70, stores the new low word at unit +0x28, and initializes the
unit's independent seed at +0x20 to {that low word, 666}. A second unit proves
that the next room draw is used. No MPQ data is read.

``--mutation-check`` requires three deliberately wrong seed models to fail.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EAX

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MASK = 0xFFFFFFFF
MULTIPLIER = 0x6AC690C5
MUTATIONS = ("no-room-step", "old-low", "shared-unit-seed")
mutation = None


def expected_step(low, high):
    """FUN_00465fd0's unsigned 64-bit room-seed step."""
    if mutation == "no-room-step":
        return low, high
    product = low * MULTIPLIER + high
    return product & MASK, product >> 32 & MASK


def run(exe):
    """Run two type-5 unit initializations and compare every seed effect."""
    machine = emu.Emu(str(exe))
    room = machine.alloc(0x100)
    unit_first = machine.alloc(0xF4)
    unit_second = machine.alloc(0xF4)
    initial_low, initial_high = 0x13579BDF, 0x29A
    machine.w32(room + 0x6C, initial_low)
    machine.w32(room + 0x70, initial_high)

    machine.hook(0x44DCC0, lambda current: 1, 0)       # expansion game
    machine.hook(0x465420, lambda current: room, 0)     # room at x, y
    machine.hook(0x61A1B0, lambda current: 1, 1)        # room's level id
    machine.hook(0x6427F0, lambda current: 0, 1)        # act from level id
    machine.hook(0x623520, lambda current: 0, 1)        # static path
    machine.hook(0x620AE0, lambda current: 0, 4)        # path position
    machine.hook(0x620A70, lambda current: 0, 4)        # footprint
    machine.hook(0x64C350, lambda current: 0, 2)        # link into room
    machine.hook(0x465F20, lambda current: current.mu.reg_read(UC_X86_REG_EAX), 0)

    expected_low, expected_high = initial_low, initial_high
    for index, unit in enumerate((unit_first, unit_second)):
        machine.call(0x465FD0, 0x1000 + index, 0x2000 + index, 31, 5, 0, 0,
                     regs={"eax": unit, "ebx": 17}, limit=5000)
        expected_low, expected_high = expected_step(expected_low, expected_high)
        unit_low = initial_low if mutation == "old-low" else expected_low
        seed_high = expected_high if mutation == "shared-unit-seed" else 0x29A
        got = (machine.r32(room + 0x6C), machine.r32(room + 0x70),
               machine.r32(unit + 0x20), machine.r32(unit + 0x24), machine.r32(unit + 0x28))
        want = (expected_low, expected_high, unit_low, seed_high, unit_low)
        if got != want:
            raise AssertionError(f"unit {index}: got {got}, expected {want}")
    return "ok: FUN_00465fd0 advanced one room seed into two independent client unit seeds"


def main():
    """Parse the executable and run stock plus negative controls."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    exe = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    print(run(exe))
    if args.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run(exe)
            except AssertionError:
                print(f"mutation {name}: failed as required")
                continue
            finally:
                mutation = None
            raise SystemExit(f"mutation {name} was not caught")


if __name__ == "__main__":
    main()
