# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the ground item cel draw FUN_0046ee80 in stock 1.14d.

Run: uv run python engine_item_cel.py [--mutation-check]
FUN_0046ee80 (ECX item unit, EDX x, one stack argument y) draws an item's
inventory cel at (x, y) and returns 1; it returns 0 for a null unit, a unit
that is not an item, a failed cel request or load, or a cel that is off
screen. A gold pile picks its frame from the amount (FUN_00625480 stat 0xe:
below 100 frame 0, below 500 frame 1, below 5000 frame 2, else 3). The cel is
drawn with mode 5, or 1 for an ethereal item, at y plus the cel height, and
the unit's overlays are then drawn at the cel's centre (FUN_0046e300 with
0xff, 0). The item-type, stat, request (FUN_004dbb50), load (FUN_006001f0),
cel size (FUN_006018f0 / FUN_006018c0), on-screen test (FUN_004dab40), colour
(FUN_0062c100), flag (FUN_006280a0), draw (FUN_004f6480) and overlay draw
callees are hooked. The claim: the gates, the gold thresholds, the argument
lists, the draw mode and the centre arithmetic.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import M32
from unicorn.x86_const import UC_X86_REG_ECX

MUT = set()
FILE = 0x6D6C1C


def suite(test):
    """Run FUN_0046ee80."""
    sel = {}
    unit = test.slot(0)

    def request(e):
        """FUN_004dbb50: note the buffers, store this case's cel in the request, give the outcome."""
        sel["req"] = e.mu.reg_read(UC_X86_REG_ECX)
        sel["p10"] = e.arg(3)
        e.w32(sel["req"] + 0x3C, sel["cel"])
        return sel["ok"]

    def colour(e):
        """FUN_0062c100: note the output pointer, give the colour."""
        sel["buf"] = e.arg(2)
        return sel["colour"]

    test._callee("kind", 0x62B400, 1, (), lambda _e: sel["kind"])
    test._callee("gold", 0x625480, 3, (), lambda _e: sel["gold"])
    test._callee("request", 0x4DBB50, 6, ("ecx", "edx"), request, nlog=6)
    test._callee("load", 0x6001F0, 3, (), lambda _e: sel["load"])
    test.callee("height", 0x6018F0, 1, ())
    test.callee("width", 0x6018C0, 1, ())
    test._callee("onscreen", 0x4DAB40, 2, ("ecx", "edx"), lambda _e: sel["screen"])
    test._callee("colour", 0x62C100, 4, (), colour, nlog=4)
    test._callee("flags", 0x6280A0, 4, (), lambda _e: sel["eth"])
    test.callee("draw", 0x4F6480, 6, ())
    test.callee("states", 0x46E300, 6, ())

    def build(img, rng):
        """A random item unit and lookups."""
        kind = rng.choice((4, 4, 4, 1, 0))
        img[unit - test.arena:unit - test.arena + 4] = kind.to_bytes(4, "little")
        sel.update(kind=rng.choice((4, 4, 3)), gold=rng.choice((0, 99, 100, 101, 499, 500, 4999, 5000, 5001, M32)),
                   ok=rng.choice((0, 1, 1, 1, 1)), load=rng.choice((0, 1, 1, 1, 1)), screen=rng.choice((0, 1, 1, 0x100)),
                   colour=rng.randrange(1 << 32), eth=rng.choice((0, 1, 0x400000)), cel=rng.randrange(1 << 32))
        return {"ecx": unit if rng.randrange(8) else 0, "edx": rng.randrange(1 << 32), "stack": [rng.randrange(1 << 32)]}

    def model(m, args):
        """The gates, the frame, the draw and the overlay call."""
        who, x, y = args["ecx"], args["edx"], args["stack"][0]
        if not who or m.r(who) != 4:
            return 0
        frame = 0
        if m.call("kind", who) == 4:
            amount = m.call("gold", who, 0xE, 0)
            if amount >= 0x64:
                if amount < (0x1F5 if "gold" in MUT else 0x1F4):
                    frame = 1
                else:
                    frame = 2 if amount < 0x1388 else 3
        if not m.call("request", sel["req"], who, 0, frame, 0, sel["p10"], 1, M32):
            return 0
        if not m.call("load", sel["req"], 0, 1):
            return 0
        cel = sel["cel"]
        if not m.call("onscreen", cel, x, (m.call("height", cel) + y) & M32, 0):
            return 0
        tint = m.call("colour", 0, who, sel["buf"], 1)
        mode = 5
        if m.call("flags", who, 0x400000, 0xAC2, FILE):
            mode = 5 if "mode" in MUT else 1
        m.call("draw", sel["req"], x, (m.call("height", cel) + y) & M32, M32, mode, tint)
        high = m.call("height", cel) >> 1
        wide = m.call("width", cel) >> (0 if "half" in MUT else 1)
        m.call("states", who, 0xFF, 0, (wide + x) & M32, (high + y) & M32, 0)
        return 1
    test.run("FUN_0046ee80", 0x46EE80, build, model, 1)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 1500
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("gold", "mode", "half")}
    rig.run_suite(suite, mutations, "ENGINE item cel", args.mutation_check)


if __name__ == "__main__":
    main()
