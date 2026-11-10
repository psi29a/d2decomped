# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_0058bc80's gated quest portal attempt in stock game.exe 1.14d.

Run: python quest_arreat_portal.py [--mutation-check]
Quest/room/level, coordinate getters and portal creator are hooked. The
synthetic unit and callback results test this helper, not its callees.
"""
import argparse

import _dungeon_rig as rig_mod

MUTATIONS = set()
REC, DATA, GAME, UNIT, PATH, ROOM, LEVEL = 0x100, 0x200, 0x400, 0x500, 0x600, 0x700, 0x800


def put(img, offset, value, size=4):
    """Store a little-endian unsigned integer at an arena offset."""
    img[offset:offset + size] = value.to_bytes(size, "little")


def suite(rig):
    """Test flag, act, path type and portal result combinations."""
    base = rig.arena
    pick = {}
    for name, addr, stack, regs in (("quest", 0x543640, 0, ("ecx", "edx")), ("room", 0x620bb0, 1, ()),
                                    ("level", 0x61a1b0, 1, ()), ("act", 0x6427f0, 1, ()),
                                    ("x", 0x6488c0, 1, ()), ("y", 0x648900, 1, ()),
                                    ("portal", 0x56d130, 7, ("ecx", "edx"))):
        rig.callee(name, addr, stack, regs)
    for name, fn in (("quest", lambda emu: base + REC), ("room", lambda emu: base + ROOM),
                     ("level", lambda emu: base + LEVEL), ("act", lambda emu: pick["act"]),
                     ("x", lambda emu: pick["x"]), ("y", lambda emu: pick["y"]),
                     ("portal", lambda emu: pick["portal"])):
        rig.custom[name] = fn
    cases = [(kind, flag, act, present, portal) for kind in (0, 1, 2, 3, 4, 5, 6, 0xffffffff)
             for flag in (0, 1) for act in (3, 4, 0x104) for present in (0, 1) for portal in (0, 1)]

    def build(img, rng):
        """Set a full combination; static path types always have valid paths."""
        kind, flag, act, present, portal = cases[rig.cases % len(cases)]
        static = kind in (2, 4, 5)
        path = base + PATH if present or static else 0
        pick.update(act=act, portal=portal, x=rng.randrange(1 << 32), y=rng.randrange(1 << 32))
        put(img, REC + 0x18, base + DATA)
        put(img, DATA + 0x89, flag, 1)
        put(img, UNIT, kind)
        put(img, UNIT + 0x2c, path)
        put(img, PATH + 0xc, rng.randrange(1 << 32))
        put(img, PATH + 0x10, rng.randrange(1 << 32))
        return {"ecx": base + GAME, "edx": base + UNIT}

    def model(m, args):
        """Create an act-5 portal at an offset from the unit's coordinates."""
        game, unit = args["ecx"], args["edx"]
        record = m.call("quest", game, 0x22)
        data = m.r(record + 0x18)
        if not m.r(data + 0x89, 1):
            return
        room = m.call("room", unit)
        level = m.call("level", room)
        if m.call("act", level) & 0xff != 4:
            return
        kind, path = m.r(unit), m.r(unit + 0x2c)
        if kind in (2, 4, 5):
            x, y = m.r(path + 0xc), m.r(path + 0x10)
        elif path:
            y = m.call("y", path)
            x = m.call("x", path)
        else:
            x = y = 0
        room = m.call("room", unit)
        portal = m.call("portal", game, 0, room, x + (11 if "x" in MUTATIONS else 10), y + 5,
                        0x79, 0, 0x3c, 1 if "flag" not in MUTATIONS else 0)
        if portal and "clear" not in MUTATIONS:
            m.w(data + 0x89, 0, 1)

    for _ in range(len(cases) // 24):
        rig.run("FUN_0058bc80", 0x58bc80, build, model, 0, ret=False)
    return rig.cases


def mutation(name):
    """Set or clear a deliberate model defect."""
    return lambda on: MUTATIONS.add(name) if on else MUTATIONS.discard(name)


def main():
    """Run stock comparisons or demonstrate detection of model defects."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, {name: mutation(name) for name in ("x", "flag", "clear")},
                      "quest portal", args.mutation_check)


if __name__ == "__main__":
    main()
