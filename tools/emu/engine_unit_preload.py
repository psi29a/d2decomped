# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the per-unit COF loaders FUN_00470610 and FUN_00470b70 in stock 1.14d.

Run: uv run python engine_unit_preload.py [--mutation-check]
FUN_00470610 (ECX unit) loads the unit's current mode by its transformed type:
a player through FUN_0046e740 then marks the layer flag (FUN_004db1c0), a
monster through FUN_0046f9e0, an object through FUN_0046e980, any other type is
fatal 0xd26 (transform line 0xd17). FUN_00470b70 (ECX unit, transform line
0x1072) resets a monster's preload slot (done and pending words at
0x7a80fc[(mode + class * 40)] and +0x50) when its class is below the header
count, then calls FUN_0046feb0 with 1 only when the unit has a room
(FUN_00620bb0) and neither FUN_004f6070 nor FUN_00454840 is set, else 0. The
transform, mode getter, loaders and queries are hooked. The claim: the
dispatch, the arguments, the slot reset, the flag and the bytes popped.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import s32

MUT = set()
FILE = 0x6D6C1C


def suite(test):
    """Run both functions."""
    sel = {}
    unit, out = test.slot(0), test.slot(1)
    hdr, table = test.arena + 0x1000, test.arena + 0x2000

    def transform(e):
        """Note the site, write this case's type / class / mode."""
        sel["site"] = (e.arg(4), e.arg(5))
        sel["seen"] = (e.r32(e.arg(1)), e.r32(e.arg(2)))
        e.w32(e.arg(1), sel["type"])
        e.w32(e.arg(2), sel["cls"])
        e.w32(e.arg(3), sel["mode"])
        return 0

    test._callee("mode", 0x621190, 1, (), lambda _e: 0x44)
    test._callee("transform", 0x645270, 6, (), transform, nlog=1)
    test._callee("player", 0x46E740, 2, ("eax",), lambda _e: sel["ok"], nlog=0)
    test.callee("setflag", 0x4DB1C0, 1, ("ecx", "edx"))
    test.callee("monster", 0x46F9E0, 0, ("eax", "ecx"))
    test.callee("object", 0x46E980, 0, ("esi", "edi"))
    test._callee("room", 0x620BB0, 1, (), lambda _e: sel["room"])
    test._callee("f6070", 0x4F6070, 0, (), lambda _e: sel["f6070"])
    test._callee("f4840", 0x454840, 0, (), lambda _e: sel["f4840"])
    test.callee("walk", 0x46FEB0, 1, ("eax",))

    def build_dispatch(img, rng):
        """A unit of any type."""
        sel.update(type=rng.choice((0, 1, 2, 3)), cls=rng.randrange(1 << 32), mode=rng.randrange(1 << 32), ok=rng.randrange(2))
        return {"ecx": unit}

    def dispatch(m, args):
        """Load by type."""
        m.call("mode", unit)
        m.call("transform", unit)
        rig.require(sel["site"] == (FILE, 0xD17), "transform site")
        rig.require(sel["seen"] == (m.r(unit), m.r(unit + 4)), "transform locals")
        kind, mode = sel["type"], sel["mode"]
        if kind == 0:
            if m.call("player", unit):
                m.call("setflag", unit, mode, 1)
        elif kind == 1:
            m.call("monster", mode, unit)
        elif kind == 2:
            m.call("object", unit, mode if "object" not in MUT else 0)
        else:
            raise rig.Halt(0xD26)
    test.run("FUN_00470610", 0x470610, build_dispatch, dispatch, 0, ret=False)

    test.m.w32(0x744304, hdr)
    test.m.w32(0x7A80FC, table)

    def build_reset(img, rng):
        """A unit, a slot table with junk, the query answers."""
        sel.update(type=rng.choice((0, 1, 1, 1)), cls=rng.choice((0xFFFFFFFF, 0, 1, 2, 3, 4)), mode=rng.randrange(20),
                   room=rng.choice((0, 1)), f6070=rng.choice((0, 0, 1)), f4840=rng.choice((0, 0, 1)))
        img[0x1000 + 0xA80:0x1000 + 0xA84] = rng.choice((0, 3, 5)).to_bytes(4, "little")
        return {"ecx": unit}

    def reset(m, args):
        """Clear the monster's slot, then walk its modes with the blocking flag."""
        m.call("mode", unit)
        m.call("transform", unit)
        rig.require(sel["site"] == (FILE, 0x1072), "transform site")
        if sel["type"] != 1 or s32(sel["cls"]) >= m.r(hdr + 0xA80):
            return
        base = table + (sel["mode"] + 40 * s32(sel["cls"])) * 4
        m.w(base, 0)
        m.w(base + (0x50 if "pending" not in MUT else 0x54), 0)
        flag = 1
        if not m.call("room", unit) or (m.call("f6070") and "flag" not in MUT) or m.call("f4840"):
            flag = 0
        m.call("walk", unit, flag)
    test.run("FUN_00470b70", 0x470B70, build_reset, reset, 0, ret=False)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 800
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("object", "pending", "flag")}
    rig.run_suite(suite, mutations, "ENGINE unit preload", args.mutation_check)


if __name__ == "__main__":
    main()
