# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005b9cd0's object update and timer against stock 1.14d.

Run: python quest_object_timer.py [--mutation-check]
Quest, unit and object-table lookups, mode setting and event scheduling are
hooked and logged. Synthetic records cover the helper, not these callees.
"""
import argparse

import _dungeon_rig as rig_mod

MUTATIONS = set()
REC, DATA, GAME, UNIT, OBJECT = 0x100, 0x200, 0x400, 0x600, 0x700


def put(img, offset, value, size=4):
    """Store an unsigned little-endian integer at an arena offset."""
    img[offset:offset + size] = value.to_bytes(size, "little")


def suite(rig):
    """Compare absent quest, absent object and scheduled-event branches."""
    base = rig.arena
    pick = {}
    rig.callee("quest", 0x543640, 0, ("ecx", "edx"))
    rig.callee("unit", 0x552f60, 1, ("ecx", "edx"))
    rig.callee("mode", 0x624690, 2, ())
    rig.callee("object", 0x640e90, 1, ())
    rig.callee("event", 0x5417d0, 4, ("ecx", "edx"))
    rig.custom["quest"] = lambda emu: pick["record"]
    rig.custom["unit"] = lambda emu: pick["unit"]
    rig.custom["object"] = lambda emu: base + OBJECT

    def build(img, rng):
        """Cover null branches and unsigned delay/frame wraparound."""
        index = rig.cases % 24
        pick["record"] = 0 if index < 6 else base + REC
        pick["unit"] = 0 if index & 1 else base + UNIT
        put(img, REC + 0x18, base + DATA)
        put(img, DATA + 0x28, rng.randrange(1 << 32))
        put(img, UNIT + 4, rng.randrange(1 << 32))
        put(img, OBJECT + 0xdc, rng.choice((0, 0xff, 0x100, 0x80000000, 0xffffffff)))
        put(img, GAME + 0xa8, rng.choice((0, 0xffffffff, rng.randrange(1 << 32))))
        return {"ecx": base + GAME}

    def model(m, args):
        """Clear data flag and schedule object animation after its table delay."""
        game = args["ecx"]
        rec = m.call("quest", game, 0x11)
        if not rec:
            return
        data = m.r(rec + 0x18)
        m.w(data + 6, 1 if "clear" in MUTATIONS else 0, 1)
        unit = m.call("unit", game, 2, m.r(data + 0x28))
        if not unit:
            return
        m.call("mode", unit, 1)
        obj = m.call("object", m.r(unit + 4))
        shift = 7 if "delay" in MUTATIONS else 8
        when = ((m.r(obj + 0xdc) >> shift) + m.r(game + 0xa8) + 1) & 0xffffffff
        m.call("event", game, unit, 1, when, 0, 0)
        m.w(data + 0x2c, 3 if "state" in MUTATIONS else 2)

    rig.run("FUN_005b9cd0", 0x5b9cd0, build, model, 0, ret=False)
    return rig.cases


def mutation(name):
    """Set or clear a deliberate model defect."""
    return lambda on: MUTATIONS.add(name) if on else MUTATIONS.discard(name)


def main():
    """Run stock comparisons or demonstrate detection of model defects."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, {name: mutation(name) for name in ("clear", "delay", "state")},
                      "quest object timer", args.mutation_check)


if __name__ == "__main__":
    main()
