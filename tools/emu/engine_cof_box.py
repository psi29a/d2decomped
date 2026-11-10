# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the unit COF box tests FUN_00470860 and FUN_004709a0 in stock 1.14d.

Run: uv run python engine_cof_box.py [--mutation-check]
FUN_00470860 is "is the point inside the unit's COF box grown by 16",
FUN_004709a0 "is the unit's COF box on screen" (an optional half-width
shift, 0x71146c / 0x711470 the screen size). Both first find the unit's COF
node for its (transformed) mode, then the box record of its COF data
(FUN_004db040). The state, owner, token, node and loader lookups, the
transform, the mode getter, the graphics-record getter and FUN_004db040 are
hooked with this case's synthetic values. The claim: the lookup order and
arguments, the unit-or-owner token source, the fatal lines (0xda4, 0xdd3),
every compare (signed, with 32-bit wrap) and the stack bytes popped.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import s32

MUT = set()
FILE = 0x6D6C1C


def suite(test):
    """Run both box tests over random units, lists, boxes and points."""
    sel = {}
    unit, gfx, owner, boxrec = test.slot(0), test.slot(1), test.slot(2), test.slot(3)
    nodes, graphics = (test.slot(4), test.slot(5), test.slot(6)), (test.slot(7), test.slot(8), test.slot(9))

    def transform(emu):
        """Remember the locals, write this case's mode."""
        sel["seen"] = (emu.r32(emu.arg(1)), emu.r32(emu.arg(2)))
        sel["site"] = (emu.arg(4), emu.arg(5))
        emu.w32(emu.arg(3), sel["mode"])
        return 0

    def token(emu):
        """FUN_0064f380: note the output pointer, give this case's token."""
        sel["buf"] = emu.arg(2)
        return sel["tok"]

    test._callee("hastate", 0x63A7B0, 2, (), lambda _emu: sel["hastate"])
    test._callee("owner", 0x4639D0, 0, ("ecx",), lambda _emu: sel["owner"])
    test._callee("token", 0x64F380, 5, (), token)
    test._callee("node", 0x46E5A0, 1, ("ecx", "edx"))
    test._callee("load", 0x46FA80, 1, ("ecx", "edx"))
    test._callee("mode", 0x621190, 1, (), lambda _emu: 0x77)
    test._callee("transform", 0x645270, 6, (), transform, nlog=1)
    test._callee("gfxinfo", 0x463E10, 0, ("ecx",), lambda _emu: sel["gfx"])
    test._callee("box", 0x4DB040, 0, ("ecx",), lambda _emu: sel["box"])

    def build(img, rng):
        """Random unit, optional owner, a COF list around the case's mode, a box near its edges."""
        def put(addr, value):
            """Store one arena dword."""
            img[addr - test.arena:addr - test.arena + 4] = (value & rig.M32).to_bytes(4, "little")
        sel.update(hastate=rng.choice((0, 1, 1)), owner=owner if rng.randrange(4) else 0, tok=rng.randrange(1 << 32),
                   gfx=gfx if rng.randrange(6) else 0, mode=rng.randrange(4), box=boxrec if rng.randrange(8) else 0)
        put(unit + 0xC8, rng.choice((0, 8, 0xFFFFFFFF, 0x10)))
        put(gfx + 0x28, nodes[0])
        for i, node in enumerate(nodes):
            put(node, rng.randrange(4))
            put(node + 0x10, nodes[i + 1] if i + 1 < len(nodes) else 0)
            put(node + 8, graphics[i])
            put(graphics[i] + 0x14, rng.choice((0, test.arena + 0x3000)))
        edges = (0, 1, -1, 5, -5, 0x10, -0x10, 0x7FFFFFFF, -0x80000000, 100, -100)
        for off in (8, 0xC, 0x10, 0x14):
            put(boxrec + off, rng.choice(edges))
        test.m.w32(0x71146C, rng.choice((0, 1, 640, 800, 0x7FFFFFFF)))
        test.m.w32(0x711470, rng.choice((0, 1, 480, 600, 0x7FFFFFFF)))
        point = lambda: rng.choice(edges + (rng.randrange(-300, 300),))
        xoff, yoff = point(), point()
        box = [int.from_bytes(img[boxrec - test.arena + o:boxrec - test.arena + o + 4], "little") for o in (8, 0xC, 0x10, 0x14)]

        def near(low, high, off):
            """A coordinate on or next to the grown box edges (16 out), else any."""
            base = rng.choice((low + off - 0x10, high + off + 0x10, low + off, high + off))
            return (base + rng.choice((-1, 0, 1))) & rig.M32 if rng.randrange(4) else point()
        args = {"ecx": unit, "edx": xoff, "stack": [yoff, near(box[0], box[1], xoff), near(box[2], box[3], yoff)]}
        if rng.randrange(2):                    # put the screen width next to the (shifted) left edge
            width = s32(box[3] - box[2])
            left = box[0] + xoff - ((width + (1 if width < 0 else 0)) >> 1 if args["stack"][2] else 0)
            test.m.w32(0x71146C, left + 1 + rng.choice((-1, 0, 1)))
        return args

    def find(m, owner_unit, line):
        """The shared lookup chain; returns the box record or None when the unit has no COF data."""
        regs_unit = unit
        token_owner = regs_unit
        if (m.r(unit + 0xC8) >> 3) & 1:
            if m.call("hastate", unit, 0x25):
                found = m.call("owner", unit)
                if found and "owner" not in MUT:
                    token_owner = found
        tok = m.r(token_owner + 0x60)
        m.call("token", unit, tok, sel["buf"], 0xFFFFFFFF, 1)
        sel_tok = sel["tok"]
        if not m.call("node", unit, sel_tok, 0xFFFFFFFF):
            m.call("load", unit, sel_tok, 0xFFFFFFFF)
        m.call("mode", unit)
        m.call("transform", unit)
        rig.require(sel["site"] == (FILE, line), "transform site")
        rig.require(sel["seen"] == (m.r(unit), m.r(unit + 4)), "transform locals")
        if not m.call("gfxinfo", unit):
            return None
        node = m.r(gfx + 0x28)
        while node:
            if m.r(node) == sel["mode"]:
                break
            node = m.r(node + 0x10)
        else:
            return None
        data = m.r(m.r(node + 8) + 0x14)
        if not data:
            return None
        rec = m.call("box", data)
        if not rec:
            raise rig.Halt(0xDA4 if line == 0xD90 else 0xDD3)
        return rec

    def inside(m, args):
        """FUN_00470860: point inside the box grown by 16."""
        rec = find(m, None, 0xD90)
        if rec is None:
            return 0
        xoff, (yoff, px, py) = args["edx"], args["stack"]
        grow = 0x11 if "grow" in MUT else 0x10
        if s32(px) < s32(m.r(rec + 8) + xoff - grow) or s32(px) >= s32(m.r(rec + 0xC) + xoff + 0x10):
            return 0
        if s32(py) < s32(m.r(rec + 0x10) + yoff - 0x10) or s32(py) >= s32(m.r(rec + 0x14) + yoff + 0x10):
            return 0
        return 1
    test.run("FUN_00470860", 0x470860, build, inside, 3)

    def on_screen(m, args):
        """FUN_004709a0: the box overlaps the screen, with an optional half-width shift."""
        rec = find(m, None, 0xDC2)
        if rec is None:
            return 0
        xoff, (yoff, _unused, half) = args["edx"], args["stack"]
        left = (m.r(rec + 8) + xoff) & rig.M32
        if half:
            width = s32(m.r(rec + 0x14) - m.r(rec + 0x10))
            shift = width // 2 if "trunc" in MUT else (width + (1 if width < 0 else 0)) >> 1
            left = (left - shift) & rig.M32
        w, h = test.m.r32(0x71146C), test.m.r32(0x711470)
        if s32(left) >= s32(w - 1):
            return 0
        if (m.r(rec + 0xC) + xoff) & 0x80000000:
            return 0
        if (m.r(rec + 0x14) + yoff) & 0x80000000:
            return 0
        return int(s32(m.r(rec + 0x10) + yoff) < s32(h - 1))
    test.run("FUN_004709a0", 0x4709A0, build, on_screen, 3)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 1500
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("grow", "trunc", "owner")}
    rig.run_suite(suite, mutations, "ENGINE COF box", args.mutation_check)


if __name__ == "__main__":
    main()
