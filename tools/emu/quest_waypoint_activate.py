# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005b4ff0 waypoint activation against stock game.exe 1.14d.

Run: python quest_waypoint_activate.py [--mutation-check]
Waypoint index lookup, player-data accessor and waypoint setter are hooked.
Their arguments are checked; their internal behavior is outside the claim.
"""
import argparse

import _dungeon_rig as rig_mod

MUTATIONS = set()
GAME, UNIT, PC, BUFFERS = 0x100, 0x300, 0x400, 0x900


def put(img, offset, value):
    """Store a little-endian unsigned dword at an arena offset."""
    img[offset:offset + 4] = value.to_bytes(4, "little")


def suite(rig):
    """Check the lookup gate and difficulty-selected waypoint buffer."""
    base = rig.arena
    pick = {}
    rig._callee("index", 0x660e00, 2, (), nlog=1)
    rig.callee("player", 0x6221a0, 1, ())
    rig.callee("set", 0x660ec0, 2, ())

    def index_hook(emu):
        """The callee writes the chosen index into the caller's stack local."""
        emu.w32(emu.arg(1), pick["index"])
        return pick["found"]

    rig.custom["index"] = index_hook
    rig.custom["player"] = lambda emu: base + PC

    def build(img, rng):
        """Vary the result, index and all 256 difficulty byte values."""
        number = rig.cases
        diff = (0, 1, 2, 3, 255, rng.randrange(256))[number % 6]
        pick["found"] = (0, 1, 2)[(number // 6) % 3]
        pick["index"] = rng.randrange(1 << 32)
        img[GAME + 0x6d] = diff
        put(img, PC + 0x1c + diff * 4, base + BUFFERS)
        return {"stack": [base + GAME, base + UNIT], "index": pick["index"]}

    def model(m, args):
        """When level 0x6d has an index, set it in the selected difficulty buffer."""
        found = m.call("index", 0x6c if "level" in MUTATIONS else 0x6d)
        if found:
            player = m.call("player", args["stack"][1])
            diff = m.r(args["stack"][0] + 0x6d, 1)
            if "diff" in MUTATIONS:
                diff = 0
            buffer = m.r(player + 0x1c + 4 * diff)
            m.call("set", buffer, args["index"] + (1 if "index" in MUTATIONS else 0))

    rig.run("FUN_005b4ff0", 0x5b4ff0, build, model, 2, ret=False)
    return rig.cases


def mutation(name):
    """Set or clear a deliberate model defect."""
    return lambda on: MUTATIONS.add(name) if on else MUTATIONS.discard(name)


def main():
    """Run the stock comparisons or demonstrate model defect detection."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, {name: mutation(name) for name in ("level", "diff", "index")},
                      "quest waypoint activation", args.mutation_check)


if __name__ == "__main__":
    main()
