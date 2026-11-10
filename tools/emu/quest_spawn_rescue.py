# SPDX-License-Identifier: GPL-3.0-or-later
"""Check quest 0x21 spawn attempt FUN_0058a820 in stock game.exe 1.14d.

Run: python quest_spawn_rescue.py [--mutation-check]
Quest lookup, unit room, room-at-position and unit spawn are hooked and
logged. Synthetic records cover their boundary effects, not their internals.
"""
import argparse

import _dungeon_rig as rig_mod

MUTATIONS = set()
REC, DATA, GAME, UNIT, ROOM, SPAWN = 0x100, 0x200, 0x400, 0x500, 0x600, 0x700


def put(img, offset, value, size=4):
    """Store an unsigned little-endian integer at the arena offset."""
    img[offset:offset + size] = value.to_bytes(size, "little")


def suite(rig):
    """Exercise missing records, flags, room lookup and spawn results."""
    base = rig.arena
    pick = {}
    rig.callee("lookup", 0x543640, 0, ("ecx", "edx"))
    rig.callee("unit_room", 0x620bb0, 1, ())
    rig.callee("room_at", 0x463740, 1, ("ecx", "edx"))
    rig.callee("spawn", 0x555230, 7, ("ecx", "edx"))
    rig.custom["lookup"] = lambda emu: pick["record"]
    rig.custom["unit_room"] = lambda emu: base + ROOM
    rig.custom["room_at"] = lambda emu: pick["room"]
    rig.custom["spawn"] = lambda emu: pick["spawn"]

    def build(img, rng):
        """Cover each combination and wraparound coordinates."""
        index = rig.cases % 24
        pick["record"] = 0 if index < 4 else base + REC
        pick["room"] = base + ROOM if index & 4 else 0
        pick["spawn"] = base + SPAWN if index & 8 else 0
        put(img, REC + 0x18, base + DATA)
        put(img, DATA + 0x84, (index >> 1) & 1)
        put(img, DATA + 0xb1, index & 1, 1)
        put(img, DATA + 0xc4, rng.choice((0, 0xfffffffe, rng.randrange(1 << 32))))
        put(img, DATA + 0xc8, rng.choice((0, 0xffffffff, rng.randrange(1 << 32))))
        put(img, SPAWN + 0xc, rng.randrange(1 << 32))
        return {"ecx": base + GAME, "edx": base + UNIT}

    def model(m, args):
        """Find a room, then spawn; record success or advance failed coordinates."""
        record = m.call("lookup", args["ecx"], 0x21)
        if not record:
            return 0
        data = m.r(record + 0x18)
        if m.r(data + 0xb1, 1):
            return 1
        if not m.r(data + 0x84):
            return 0
        unit_room = m.call("unit_room", args["edx"])
        x, y = m.r(data + 0xc4), m.r(data + 0xc8)
        room = m.call("room_at", unit_room, x, y)
        if not room:
            m.w(data + 0xc4, x + (2 if "advance" in MUTATIONS else 3))
            m.w(data + 0xc8, y + 3)
            return 0
        spawn = m.call("spawn", 2, 0xbd, x, y, args["ecx"], room, 1, 1,
                       1 if "spawn-flag" in MUTATIONS else 0)
        if not spawn:
            return 0
        m.w(data + 0xb1, 1, 1)
        m.w(data + 0xc0, m.r(spawn + (8 if "unit-id" in MUTATIONS else 0xc)))
        return 1

    rig.run("FUN_0058a820", 0x58a820, build, model, 0)
    return rig.cases


def mutation(name):
    """Set or clear a deliberate model defect."""
    return lambda on: MUTATIONS.add(name) if on else MUTATIONS.discard(name)


def main():
    """Run stock comparisons or demonstrate detection of deliberate defects."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, {name: mutation(name) for name in ("advance", "spawn-flag", "unit-id")},
                      "quest rescue spawn", args.mutation_check)


if __name__ == "__main__":
    main()
