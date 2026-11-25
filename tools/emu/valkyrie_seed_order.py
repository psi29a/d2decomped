# SPDX-License-Identifier: GPL-3.0-or-later
"""Valkyrie's FUN_00573cb0 life draw precedes her MonEquip seed walk.

    .venv/bin/python valkyrie_seed_order.py --mutation-check

Runs FUN_00573cb0 itself on a synthetic Valkyrie-shaped monster. The look
boundary is hooked to no seed step, matching Valkyrie's sixteen zero variant
counts; FUN_006538a0 supplies a synthetic life range.
The life draw and the call through FUN_005d6b60 are native. Other stat-list,
room, property and event effects are hooked. Mutation checking requires
three wrong order/range models to fail.
"""
import argparse
import struct

import emu

MASK = 0xFFFFFFFF
MULTIPLIER = 0x6AC690C5
MUTATIONS = ("extra-look", "missing-life", "inclusive-high")


def step(low, high):
    product = low * MULTIPLIER + high
    return product & MASK, product >> 32 & MASK


def draw(low, high, bound):
    low, high = step(low, high)
    value = low & (bound - 1) if bound & (bound - 1) == 0 else low % bound
    return value, low, high


class Rig:
    def __init__(self):
        self.machine = machine = emu.Emu()
        data = machine.alloc(0x2000)
        monstats = machine.alloc(0x1A8)
        monstats2 = machine.alloc(0x134)
        monster_data = machine.alloc(0x100)
        self.unit = machine.alloc(0x200)
        self.game = machine.alloc(0x200)
        state_node = machine.alloc(0x40)
        machine.w32(0x744304, data)
        machine.w32(data + 0xA78, monstats)
        machine.w32(data + 0xA80, 1)
        machine.w32(data + 0xA90, monstats2)
        machine.w32(data + 0xA98, 1)
        machine.mu.mem_write(monstats + 0x18, struct.pack("<h", 0))
        machine.mu.mem_write(monstats + 0x0C, bytes((4,)))  # noRatio
        machine.mu.mem_write(monstats + 0xAA, struct.pack("<h", 1))
        machine.w32(monster_data, monstats)
        machine.w32(self.unit, 1)
        machine.w32(self.unit + 0x14, monster_data)
        self.life = None
        self.gear_seed = None

        machine.hook(0x5739D0, self.look, 0)
        machine.hook(0x573930, lambda current: 0, 2)       # player-count additions: zero
        machine.hook(0x6538A0, self.ranges, 6)
        machine.hook(0x627260, self.set_stat, 4)
        machine.hook(0x5D6B60, self.gear, 5)
        machine.stub(0, {0x483360: 1, 0x573470: 0, 0x626D40: 4, 0x626E10: 3,
                         0x63ABD0: 2, 0x63EE90: 1, 0x63EEF0: 3, 0x6438B0: 1})
        machine.hook(0x6251F0, lambda current: state_node, 5)

    def look(self, current):
        # FUN_005739d0 calls rand-below with all sixteen bounds zero; those
        # calls return without stepping FUN_0045c3e0's seed.
        return None

    @staticmethod
    def ranges(current):
        output = current.arg(5)
        current.mu.mem_write(output, bytes(0x38))
        current.w32(output, 400)
        current.w32(output + 4, 480)
        return 1

    def set_stat(self, current):
        if current.arg(1) == 7:
            self.life = current.arg(2)

    def gear(self, current):
        self.gear_seed = (current.r32(self.unit + 0x20), current.r32(self.unit + 0x24))

    def run(self, seed_low, seed_high):
        self.machine.w32(self.unit + 0x20, seed_low)
        self.machine.w32(self.unit + 0x24, seed_high)
        self.life = self.gear_seed = None
        self.machine.call(0x573CB0, self.game, 0, 0, regs={"esi": self.unit}, limit=100000)
        return self.life, self.gear_seed


def expected(seed_low, seed_high, mutation=None):
    low, high = seed_low, seed_high
    if mutation == "extra-look":
        low, high = step(low, high)
    if mutation == "missing-life":
        life_roll = 0
    else:
        bound = 82 if mutation == "inclusive-high" else 81
        life_roll, low, high = draw(low, high, bound)
    return (400 + life_roll) << 8, (low, high)


def check(mutation=None):
    rig = Rig()
    cases = 0
    for seed_low in (1, 2, 7, 19, 0x12345678, 0xFFFFFFFF):
        for seed_high in (666, 0x89ABCDEF):
            got = rig.run(seed_low, seed_high)
            want = expected(seed_low, seed_high, mutation)
            if got != want:
                raise AssertionError(f"seed {seed_low}:{seed_high}: got {got}, expected {want}")
            cases += 1
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    print(f"ok: {check()} FUN_00573cb0 Valkyrie seed-order cases")
    if args.mutation_check:
        for mutation in MUTATIONS:
            try:
                check(mutation)
            except AssertionError:
                print(f"mutation {mutation}: failed as required")
            else:
                raise SystemExit(f"mutation {mutation} was not caught")


if __name__ == "__main__":
    main()
