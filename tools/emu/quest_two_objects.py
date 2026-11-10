# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005bcfd0 quest object updates against stock game.exe 1.14d.

Run: python quest_two_objects.py [--mutation-check]
Quest lookup, unit lookup and mode setter are hooked and logged. Synthetic
records and scripted lookup results exercise branches without running callees.
"""
import argparse

import _dungeon_rig as rig_mod

MUTATIONS = set()
REC, DATA, GAME, UNIT1, UNIT2 = 0x100, 0x200, 0x400, 0x500, 0x600


def put(img, offset, value, size=4):
    """Store a little-endian unsigned integer at an arena offset."""
    img[offset:offset + size] = value.to_bytes(size, "little")


def suite(rig):
    """Compare record absence and both objects' flag and lookup combinations."""
    base = rig.arena
    pick = {}
    rig.callee("quest", 0x543640, 0, ("ecx", "edx"))
    rig.callee("unit", 0x552f60, 1, ("ecx", "edx"))
    rig.callee("mode", 0x624690, 2, ())
    rig.custom["quest"] = lambda emu: pick["record"]
    rig.custom["unit"] = lambda emu: pick[emu.arg(0)]

    def build(img, rng):
        """Cover all flag and lookup combinations and missing records."""
        index = rig.cases % 24
        pick["record"] = 0 if index >= 16 else base + REC
        pick[0x1234] = base + UNIT1 if index & 4 else 0
        pick[0x5678] = base + UNIT2 if index & 8 else 0
        put(img, REC + 0x18, base + DATA)
        put(img, DATA + 1, index & 1, 1)
        put(img, DATA + 2, (index >> 1) & 1, 1)
        put(img, DATA + 4, 0x1234)
        put(img, DATA + 8, 0x5678)
        return {"ecx": base + GAME}

    def model(m, args):
        """Set the two state dwords and update each flagged, found object."""
        rec = m.call("quest", args["ecx"], 0x14)
        if not rec:
            return
        data = m.r(rec + 0x18)
        m.w(data + 0xc, 1 if "state" in MUTATIONS else 2)
        m.w(data + 0x10, 2)
        for index in range(2):
            if m.r(data + 1 + index, 1):
                unit = m.call("unit", args["ecx"], 2, m.r(data + 4 + index * 4))
                if unit:
                    mode = 1 if "mode" in MUTATIONS else index + 1
                    m.call("mode", unit, mode)
                elif "null" in MUTATIONS:
                    m.call("mode", unit, index + 1)

    rig.run("FUN_005bcfd0", 0x5bcfd0, build, model, 0, ret=False)
    return rig.cases


def mutation(name):
    """Set or clear a deliberate model defect."""
    return lambda on: MUTATIONS.add(name) if on else MUTATIONS.discard(name)


def main():
    """Run stock comparisons or demonstrate deliberate defects are detected."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, {name: mutation(name) for name in ("state", "mode", "null")},
                      "quest two objects", args.mutation_check)


if __name__ == "__main__":
    main()
