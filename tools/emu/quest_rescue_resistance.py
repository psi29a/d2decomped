# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_0058a0a0's three difficulty reward lookups in stock 1.14d.

Run: python quest_rescue_resistance.py [--mutation-check]
Player-data access, quest-bit test, and reward application are hooked and
logged; this does not check their internals or the reward's later effects.
"""
import argparse

import _dungeon_rig as rig_mod

MUTATIONS = set()


def suite(rig):
    """Compare the stock helper and model across every three-bit combination."""
    base = rig.arena
    player_data, game, unit = base + 0x100, base + 0x200, base + 0x300
    answers = {}
    rig.callee("player_data", 0x6221a0, 1, ())
    rig.callee("bit", 0x65c310, 3, ())
    rig.callee("reward", 0x589ff0, 1, ("ecx", "edx"))
    rig.custom["player_data"] = lambda emu: player_data
    rig.custom["bit"] = lambda emu: answers[emu.arg(0)]

    def build(img, rng):
        """Choose all eight combinations and distinct nonzero result values."""
        bits = rig.cases % 8
        for index in range(3):
            pointer = base + 0x400 + 4 * index
            img[0x110 + 4 * index:0x114 + 4 * index] = pointer.to_bytes(4, "little")
            answers[pointer] = rng.choice((1, 2, 0x80000000)) if bits & (1 << index) else 0
        return {"ecx": game, "edx": unit}

    def model(m, args):
        """Add ten for every difficulty whose quest 0x25 bit 7 is nonzero."""
        total = 0
        for index in range(3):
            pc = m.call("player_data", args["edx"])
            bit = m.call("bit", m.r(pc + 0x10 + index * 4), 0x24 if "quest" in MUTATIONS else 0x25, 7)
            if bit:
                total += 11 if "amount" in MUTATIONS else 10
        if total or "always" in MUTATIONS:
            m.call("reward", args["ecx"], args["edx"], total)

    rig.run("FUN_0058a0a0", 0x58a0a0, build, model, 0, ret=False)
    return rig.cases


def mutation(name):
    """Set or clear one intentional model defect."""
    return lambda on: MUTATIONS.add(name) if on else MUTATIONS.discard(name)


def main():
    """Run stock comparisons or check that model defects are caught."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, {name: mutation(name) for name in ("amount", "always", "quest")},
                      "quest resistance", args.mutation_check)


if __name__ == "__main__":
    main()
