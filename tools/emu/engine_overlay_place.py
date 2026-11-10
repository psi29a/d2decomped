# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the positioned overlay draw FUN_0046e300 in stock 1.14d.

Run: uv run python engine_overlay_place.py [--mutation-check]
FUN_0046e300 (stack: unit, mode, match flag, x, y, shift flag) draws the
unit's overlays at a screen position. A unit without a graphics record is
fatal 0x271. With FUN_004f51d0 set and a unit that is null, not an item or
an item in mode 3, the position comes from three conversions (FUN_00620410,
FUN_006203b0, FUN_004f6760, written through the x / y argument slots) with a
quarter-screen shift for the two side-panel modes and 16 down; otherwise it
is the position getters minus the sub-offsets, replaced by the x / y
arguments for an item when neither is -1. The three offset getters are added
and 8 is taken off y with the shift flag. Each overlay node is drawn when
its +0x34 flag agrees with the match flag, kind 6 needs +8 and kind 8 needs
+0x3c: the Overlay.txt row (0x744304; a bad row or a null entry is fatal
0x2d1), the cel request (FUN_004dbb50 after FUN_00620100, which ends the
function when it fails) and FUN_004f6480 at the node offsets when the frame
is below the node's +0x1c, after the FUN_0044dc60 / FUN_00477680 pair when
the row's +0x81 is set. Every callee is hooked. The claim: both position
paths, the node filters, the fatals, the order and arguments, and the end.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import M32, s32
from unicorn.x86_const import UC_X86_REG_ECX

MUT = set()


def suite(test):
    """Run FUN_0046e300."""
    sel = {}
    unit, gfx = test.slot(0), test.slot(1)
    slots = (2, 3, 4)
    hdr, rows = test.arena + 0x1000, test.arena + 0x2000

    def convert(e):
        """FUN_00620410: write this case's position through the x / y argument slots."""
        sel["px"], sel["py"] = e.arg(2), e.arg(3)
        e.w32(e.arg(2), sel["a"])
        e.w32(e.arg(3), sel["b"])
        return sel["r1"]

    def request(e):
        """FUN_004dbb50: note the buffers, give the next outcome."""
        sel["req"] = e.mu.reg_read(UC_X86_REG_ECX)
        sel["p14"] = e.arg(3)
        return sel["ok"].pop(0)

    test._callee("gfxinfo", 0x463E10, 0, ("ecx",), lambda _e: sel["gfx"])
    test._callee("persp", 0x4F51D0, 0, (), lambda _e: sel["persp"])
    test._callee("kind", 0x621190, 1, (), lambda _e: sel["itemmode"])
    test._callee("convert", 0x620410, 4, (), convert, nlog=2)
    test._callee("convert2", 0x6203B0, 2, ())
    test._callee("apply", 0x4F6760, 1, ())
    test._callee("pmode", 0x45AE90, 0, (), lambda _e: sel["pmode"])
    test._callee("posx", 0x620650, 1, ())
    test._callee("subx", 0x45AFC0, 0, ())
    test._callee("posy", 0x6206B0, 1, ())
    test._callee("suby", 0x45AFD0, 0, ())
    test._callee("d0b0", 0x4DA0B0, 1, ())
    test._callee("d0d0", 0x4DA0D0, 1, ())
    test._callee("d0f0", 0x4DA0F0, 1, ())
    test._callee("al", 0x620100, 1, ())
    test._callee("request", 0x4DBB50, 6, ("ecx", "edx"), request, nlog=6)
    test._callee("c60", 0x44DC60, 0, (), lambda _e: sel["c60"])
    test.callee("c680", 0x477680, 0, ())
    test.callee("draw", 0x4F6480, 6, ())

    def build(img, rng):
        """An overlay list and the unit state."""
        def put(addr, value, size=4):
            """Store an arena value."""
            off = addr - test.arena
            img[off:off + size] = (value & ((1 << 8 * size) - 1)).to_bytes(size, "little")
        edge = (0, 1, -1, 3, 100, 0x7FFFFFFF, -0x80000000, 7)
        sel.update(gfx=gfx if rng.randrange(12) else 0, persp=rng.choice((0, 1, 1)), itemmode=rng.choice((0, 3, 3, 5)),
                   a=rng.choice(edge) & M32, b=rng.choice(edge) & M32, r1=rng.randrange(1 << 32), pmode=rng.choice((0, 1, 2, 3)),
                   c60=rng.randrange(2), ok=[rng.choice((0, 1, 1, 1, 1)) for _ in range(4)])
        count = len(slots) if rng.randrange(2) else rng.randrange(len(slots) + 1)
        used = [test.slot(i) for i in slots[:count]]
        put(gfx + 0x2C, used[0] if used else 0)
        live = rng.randrange(12)        # a null table with row > 0 would fault in stock too
        put(hdr + 0xBBC, rows if live else 0)
        put(hdr + 0xBC0, rng.choice((1, 2, 3)))
        for i, n in enumerate(used):
            put(n + 0xA4, used[i + 1] if i + 1 < len(used) else 0)
            put(n, rng.choice((6, 8, 1, 1)))
            put(n + 4, rng.choice((0, 1, 2, 2, 5, 0xFFFFFFFF) if live else (0, 5, 0xFFFFFFFF)))
            put(n + 8, rng.choice((0, 5)))
            put(n + 0x34, rng.choice((0, 0, 1, 2, 0x80)))    # any nonzero value counts: stock tests, not compares with 1
            put(n + 0x3C, rng.choice((0, 1)))
            put(n + 0x18, rng.choice((0, 0x100, 0x300, 0x80000000, 0xFFFFFF00)))
            put(n + 0x1C, rng.choice((0x100, 0x200, 0x400, 0x80000000)))
            put(n + 0x20, rng.choice(edge))
            put(n + 0x24, rng.choice(edge))
        for r in range(3):
            put(rows + r * 0x84 + 0x7C, rng.randrange(256), 1)
            put(rows + r * 0x84 + 0x81, rng.choice((0, 1, 0x80)), 1)
        put(unit, rng.choice((4, 4, 4, 1, 0)))
        test.m.w32(0x744304, hdr)
        test.m.w32(0x71146C, rng.choice((0, 7, 800, 1, -5 & M32, 0x7FFFFFFF, 4)))
        stack = [unit if rng.randrange(10) else 0, rng.randrange(1 << 32), rng.choice((0, 1)),
                 rng.choice((M32, M32, 0, 5, rng.randrange(1 << 32))), rng.choice((M32, M32, 0, 5, rng.randrange(1 << 32))),
                 rng.choice((0, 1, 0x80))]
        return {"stack": stack}

    def model(m, args):
        """The position and the node walk."""
        who, mode, match, xa, ya, shift = args["stack"]
        info = m.call("gfxinfo", who)
        if not info:
            raise rig.Halt(0x271)
        persp = m.call("persp")
        node = m.r(info + 0x2C)
        if not node:
            return
        item = bool(who) and m.r(who) == 4
        plain = not persp or (item and m.call("kind", who) != 3)
        if not plain:
            r1 = m.call("convert", who, 0)
            r2 = m.call("convert2", who, r1)
            m.call("apply", r2)
            x, y = sel["a"], sel["b"]
            side = m.call("pmode")
            width = test.m.r32(0x71146C)
            quarter = (s32(width) + ((s32(width) >> 31) & 3)) >> 2
            if side == 1:
                x = (x - quarter) & M32
            elif side == 2:
                x = (x + quarter) & M32
            y = (y + 0x10) & M32
        else:
            px = m.call("posx", who)
            x = (px - m.call("subx")) & M32
            py = m.call("posy", who)
            y = (py - m.call("suby")) & M32
            if item and (xa != M32 or "item" in MUT) and ya != M32:
                x, y = xa, ya
        x = (x + m.call("d0b0", who)) & M32
        y = (y + m.call("d0d0", who)) & M32
        y = (y + m.call("d0f0", who)) & M32
        if shift:
            y = (y - (4 if "y8" in MUT else 8)) & M32
        while node:
            flag = m.r(node + 0x34)
            ok = (flag == 1 if "eq1" in MUT else bool(flag)) == bool(match or ("match" in MUT))
            kind = m.r(node)
            if kind == 6 and not m.r(node + 8):
                ok = False
            skip = (kind == 8 and not m.r(node + 0x3C)) or not ok
            if not skip:
                frame = s32(m.r(node + 0x18)) >> 8
                row = s32(m.r(node + 4))
                head = test.m.r32(0x744304)
                if row < 0 or row >= s32(m.r(head + 0xBC0)):
                    raise rig.Halt(0x2D1)
                orow = (m.r(head + 0xBBC) + row * 0x84) & M32
                if not orow:
                    raise rig.Halt(0x2D1)
                al = m.call("al", who) & 0xFF
                if not m.call("request", sel["req"], 0, 0, al, frame & M32, sel["p14"], 1, M32):
                    return
                if frame < (s32(m.r(node + 0x1C)) >> 8):
                    if m.r(orow + 0x81, 1) and m.call("c60"):
                        m.call("c680")
                    m.call("draw", sel["req"], (m.r(node + 0x20) + x) & M32, (m.r(node + 0x24) + y) & M32, mode, m.r(orow + 0x7C, 1), 0)
            node = m.r(node + 0xA4)
    test.run("FUN_0046e300", 0x46E300, build, model, 6, ret=False)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 3000
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("item", "y8", "match", "eq1")}
    rig.run_suite(suite, mutations, "ENGINE overlay place", args.mutation_check)


if __name__ == "__main__":
    main()
