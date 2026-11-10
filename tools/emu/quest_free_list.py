# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_00543590 quest list cleanup against stock game.exe 1.14d.

Run: python quest_free_list.py [--mutation-check]
The allocator free boundary is hooked and logged; synthetic acyclic lists
exercise traversal and call arguments without testing allocator internals.
"""
import argparse

import _dungeon_rig as rig_mod

MUTATIONS = set()
GAME, NODES, POOL = 0x100, 0x300, 0x600


def put(img, offset, value):
    """Store an unsigned little-endian dword at an arena offset."""
    img[offset:offset + 4] = value.to_bytes(4, "little")


def suite(rig):
    """Compare empty and nonempty acyclic lists with synthetic next links."""
    base = rig.arena

    def build(img, rng):
        """Make lists of zero through five nodes with random node payloads."""
        length = rig.cases % 6
        put(img, GAME + 0x1c, base + POOL)
        for index in range(length):
            offset = NODES + index * 0x10
            put(img, offset, rng.randrange(1 << 32))
            put(img, offset + 4, base + offset + 0x10 if index + 1 < length else 0)
        return {"ecx": base + GAME, "edx": base + NODES if length else 0, "length": length}

    def model(m, args):
        """Free each node in order using the game's pool and source line 0x50."""
        node = args["edx"]
        while node:
            next_node = m.r(node + 4)
            line = 0x51 if "line" in MUTATIONS else 0x50
            pool = args["ecx"] if "pool" in MUTATIONS else m.r(args["ecx"] + 0x1c)
            m.free(pool, node, line, 0x6e0be4)
            if "one" in MUTATIONS:
                break
            node = next_node

    rig.run("FUN_00543590", 0x543590, build, model, 0, ret=False)
    return rig.cases


def mutation(name):
    """Set or clear a deliberate model defect."""
    return lambda on: MUTATIONS.add(name) if on else MUTATIONS.discard(name)


def main():
    """Run comparisons or demonstrate detection of model defects."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, {name: mutation(name) for name in ("line", "pool", "one")},
                      "quest list cleanup", args.mutation_check)


if __name__ == "__main__":
    main()
