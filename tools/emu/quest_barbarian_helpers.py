# SPDX-License-Identifier: GPL-3.0-or-later
"""Check quest 0x21 coordinate and notification helpers in stock 1.14d.

Run: python quest_barbarian_helpers.py [--mutation-check]
Quest lookup, coordinate getter, and notification are hooked and logged;
this verifies the helpers' branching, arguments, writes and stack cleanup,
not the hooked callees' implementations.
"""
import argparse

import _dungeon_rig as rig_mod

MUTATIONS = set()
REC, DATA, GAME, UNIT, OUTPUT = 0x100, 0x200, 0x400, 0x500, 0x600


def put(img, offset, value, size=4):
    """Store an unsigned little-endian value in the synthetic arena."""
    img[offset:offset + size] = value.to_bytes(size, "little")


def suite(rig):
    """Compare stock code with models for all gate combinations."""
    base = rig.arena
    record = {}
    rig.callee("lookup", 0x543640, 0, ("ecx", "edx"))
    rig.callee("coords", 0x620870, 2, ())
    rig.callee("notify", 0x544300, 3, ("ecx", "edx"))
    rig.custom["lookup"] = lambda emu: record["pointer"]

    def build(img, rng):
        """Cover absent record and each combination of flag and state bytes."""
        index = rig.cases % 24
        record["pointer"] = 0 if index < 8 else base + REC
        put(img, REC + 0x18, base + DATA)
        put(img, REC + 9, (index >> 2) & 1, 1)
        put(img, REC + 0xb, index % 4, 1)
        put(img, DATA + 0x84, (index >> 1) & 1)
        put(img, DATA + 0xb1, index & 1, 1)
        put(img, DATA + 0xc4, rng.randrange(1 << 32))
        put(img, DATA + 0xc8, rng.randrange(1 << 32))
        return {"ecx": base + GAME, "edx": base + UNIT, "stack": [base + OUTPUT]}

    def coords(m, args):
        """Return coordinates only when quest data +0x84 is nonzero."""
        rec = m.call("lookup", args["ecx"], 0x21)
        if not rec:
            return 0
        data = m.r(rec + 0x18)
        if not m.r(data + 0x84):
            return 0
        if not m.r(data + 0xb1, 1):
            m.call("coords", args["edx"], args["stack"][0])
        else:
            m.w(args["stack"][0], m.r(data + 0xc4))
            m.w(args["stack"][0] + 4, m.r(data + (0xc4 if "coords" in MUTATIONS else 0xc8)))
        return 1

    def build_notify(img, rng):
        """Use the same record states without a stack argument."""
        args = build(img, rng)
        args["stack"] = []
        return args

    def notify(m, args):
        """When introduced with state byte below two, clear byte +0x14 and notify."""
        rec = m.call("lookup", args["ecx"], 0x21)
        if rec and m.r(rec + 9, 1) and m.r(rec + 0xb, 1) < (3 if "limit" in MUTATIONS else 2):
            m.w(rec + 0x14, 1 if "clear" in MUTATIONS else 0, 1)
            m.call("notify", rec, 2, 0, 0x588eb0, 1)

    rig.run("FUN_0058a8d0", 0x58a8d0, build, coords, 1)
    rig.run("FUN_0058aa10", 0x58aa10, build_notify, notify, 0, ret=False)
    return rig.cases


def mutation(name):
    """Set or clear a deliberate model defect."""
    return lambda on: MUTATIONS.add(name) if on else MUTATIONS.discard(name)


def main():
    """Run the comparisons or their mutation checks."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, {name: mutation(name) for name in ("coords", "limit", "clear")},
                      "quest barbarian helper", args.mutation_check)


if __name__ == "__main__":
    main()
