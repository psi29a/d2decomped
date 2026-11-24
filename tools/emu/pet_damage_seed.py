# SPDX-License-Identifier: GPL-3.0-or-later
"""A pet's physical roll in FUN_0057b420 uses its unit seed.

    .venv/bin/python pet_damage_seed.py --mutation-check

Runs FUN_0057b420 natively with synthetic attacker stats and compares its
fine physical result and final attacker seed with the model used by d2d's
player_blow damage_rng path. Stat access and unrelated damage-record effects
are hooked. Mutation checking requires a shared fight seed, an inclusive
upper bound and a missing draw to be detected.
"""
import argparse

import emu

MASK = 0xFFFFFFFF
MULTIPLIER = 0x6AC690C5
MUTATIONS = ("fight-seed", "inclusive-high", "no-draw")


def step(low, high):
    product = low * MULTIPLIER + high
    return product & MASK, product >> 32 & MASK


def below(low, high, bound):
    low, high = step(low, high)
    value = low & (bound - 1) if bound & (bound - 1) == 0 else low % bound
    return value, low, high


class Oracle:
    def __init__(self):
        self.machine = machine = emu.Emu()
        self.attacker = machine.alloc(0x100)
        self.stats = {}
        machine.hook(0x625480, lambda current: self.stats.get(current.arg(1), 0), 3)
        machine.hook(0x625500, lambda current: self.stats.get(current.arg(1), 0), 3)
        machine.stub(0, {0x535BC0: 0, 0x63D340: 2, 0x645830: 4})

    def run(self, seed_low, seed_high, low, high):
        self.stats = {0x15: low, 0x16: high}
        unit = self.machine.view("unit", self.attacker)
        unit.seed_low, unit.seed_high = seed_low, seed_high
        result = self.machine.call(0x57B420, 0, 0, 0, 0, 0, 0x80,
                                   ecx=self.attacker, edx=1)
        return result, unit.seed_low, unit.seed_high


def check(mutation=None):
    oracle = Oracle()
    checked = 0
    for seed_low in (1, 2, 7, 0x12345678, 0xFFFFFFFF):
        for seed_high in (666, 0x89ABCDEF):
            for low, high in ((1, 2), (2, 9), (7, 23), (100, 356)):
                bound = (high - low) << 8
                model_low = 0xA5A5A5A5 if mutation == "fight-seed" else seed_low
                model_high = 666 if mutation == "fight-seed" else seed_high
                if mutation == "no-draw":
                    roll, final_low, final_high = 0, model_low, model_high
                else:
                    if mutation == "inclusive-high":
                        bound += 1
                    roll, final_low, final_high = below(model_low, model_high, bound)
                expected = (low << 8) + roll
                got = oracle.run(seed_low, seed_high, low, high)
                if got != (expected, final_low, final_high):
                    raise AssertionError(f"seed {seed_low}:{seed_high} range {low}..{high}: got {got}, expected {(expected, final_low, final_high)}")
                checked += 1
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    print(f"ok: {check()} FUN_0057b420 pet-seed damage rolls")
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
