# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005ba2f0's quest-party dispatch against stock game.exe 1.14d.

Run: python quest_party_callback.py [--mutation-check]
Party lookup and member iteration are hooked and logged; the callback itself
and both callees are not exercised by this harness.
"""
import argparse

import _dungeon_rig as rig_mod

MUTATIONS = set()


def suite(rig):
    """Sweep party lookup results, including full-width and low-word sentinels."""
    base = rig.arena
    pick = {}
    rig.callee("party", 0x554630, 0, ("ecx",))
    rig.callee("iterate", 0x540510, 2, ("ecx", "edx"))
    rig.custom["party"] = lambda emu: pick["answer"]
    values = (0xffff, 0xffffffff, 0x1234ffff, 0, 1, 0x8000, 0x10000, 0x12345678)

    def build(img, rng):
        """Supply a player, game, and a party result with independently varied high bits."""
        pick["answer"] = values[rig.cases % len(values)]
        return {"ecx": base + 0x100, "edx": rng.randrange(1 << 32), "stack": [base + 0x200]}

    def model(m, args):
        """Iterate members only when the returned low word is not 0xffff."""
        answer = m.call("party", args["ecx"])
        party = answer if "width" in MUTATIONS else answer & 0xffff
        if party != 0xffff:
            callback = 0x5ba290 if "callback" not in MUTATIONS else 0x5ba291
            m.call("iterate", args["stack"][0], party, callback, 1 if "arg" in MUTATIONS else 0)
        return 0

    rig.run("FUN_005ba2f0", 0x5ba2f0, build, model, 1)
    return rig.cases


def mutation(name):
    """Set or clear a deliberate model defect."""
    return lambda on: MUTATIONS.add(name) if on else MUTATIONS.discard(name)


def main():
    """Run stock comparisons or demonstrate detection of deliberate defects."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, {name: mutation(name) for name in ("width", "callback", "arg")},
                      "quest party callback", args.mutation_check)


if __name__ == "__main__":
    main()
