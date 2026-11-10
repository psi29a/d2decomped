# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_0059c810 quest completion setup against stock game.exe 1.14d.

Run: python quest_tomb_transition.py [--mutation-check]
Bit setting, client lookup and progress setting are hooked and logged; the
helper's call sequence is checked, not its callees' downstream effects.
"""
import argparse

import _dungeon_rig as rig_mod

MUTATIONS = set()
GAME, PLAYER, FLAGS, CLIENT = 0x100, 0x300, 0x500, 0x700


def suite(rig):
    """Compare the completion helper across difficulty-byte boundaries."""
    base = rig.arena
    rig.callee("set_bit", 0x65c360, 3, ())
    rig.callee("client", 0x5531c0, 1, ("ecx", "edx"))
    rig.callee("progress", 0x538680, 1, ("ecx", "edx"))
    rig.custom["client"] = lambda emu: base + CLIENT

    def build(img, rng):
        """Place the quest buffer in ESI and vary the unsigned difficulty."""
        img[GAME + 0x6d] = (0, 1, 2, 3, 127, 255)[rig.cases % 6]
        return {"regs": {"esi": base + FLAGS}, "stack": [base + GAME, base + PLAYER]}

    def model(m, args):
        """Set quest 0xe bits 0xd and 3, then set progression from difficulty."""
        flags = args["regs"]["esi"]
        m.call("set_bit", flags, 0xe, 0xd)
        m.call("set_bit", flags, 0xe, 4 if "bit" in MUTATIONS else 3)
        client = m.call("client", args["stack"][1], 0x6e2068, 0x182)
        difficulty = m.r(args["stack"][0] + 0x6d, 1)
        m.call("progress", client, 3 if "mode" in MUTATIONS else 2,
               0 if "difficulty" in MUTATIONS else difficulty)

    rig.run("FUN_0059c810", 0x59c810, build, model, 2, ret=False)
    return rig.cases


def mutation(name):
    """Set or clear a deliberate model defect."""
    return lambda on: MUTATIONS.add(name) if on else MUTATIONS.discard(name)


def main():
    """Run comparisons or demonstrate detection of model defects."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, {name: mutation(name) for name in ("bit", "mode", "difficulty")},
                      "quest tomb transition", args.mutation_check)


if __name__ == "__main__":
    main()
