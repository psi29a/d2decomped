# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the unit colored shape draw FUN_00471a30 in stock 1.14d.

Run: uv run python engine_unit_shape.py [--mutation-check]
FUN_00471a30 (ECX unit) draws a unit's solid-colour silhouette. A null unit
is type 6, class -1; the type / class / mode are transformed (line 0x9b6).
An object takes its draw offset (+0x148 / +0x14c) from Objects.txt, only with
+0x150 and +0x118 + mode set (a missing row is fatal 0x9be). The screen
position is the position getters minus the sub-offsets; with FUN_004f51d0
set, a quarter screen is shifted for the two side-panel modes. FUN_004709a0
tests the box; FUN_00046ed50 gives the colour byte, which replaces the low
byte of the FUN_004f51d0 result as the last draw argument (FUN_004f6570).
Every callee is hooked. The claim: the early exits, the arithmetic, the
mixed colour argument, the request and the result.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import M32, s32
from unicorn.x86_const import UC_X86_REG_ECX

MUT = set()
FILE = 0x6D6C1C


def suite(test):
    """Run FUN_00471a30."""
    sel = {}
    unit, objrow = test.slot(0), test.slot(1)

    def transform(e):
        """Note the site and the locals, write this case's type / class / mode."""
        sel["site"] = (e.arg(4), e.arg(5))
        sel["seen"] = (e.r32(e.arg(1)), e.r32(e.arg(2)))
        e.w32(e.arg(1), sel["type"])
        e.w32(e.arg(2), sel["cls"])
        e.w32(e.arg(3), sel["mode"])
        return 0

    def offs(e):
        """FUN_00621810: write the value the request uses."""
        e.w32(e.arg(1), sel["v1c"])
        return 0

    def request(e):
        """FUN_004dbb50: note the buffers."""
        sel["req"] = e.mu.reg_read(UC_X86_REG_ECX)
        sel["p24"] = e.arg(3)
        return sel["ok"]

    test._callee("mode", 0x621190, 1, (), lambda _e: 0x33)
    test._callee("transform", 0x645270, 6, (), transform, nlog=1)
    test._callee("persp", 0x4F51D0, 0, (), lambda _e: sel["persp"])
    test._callee("objects", 0x640E90, 1, (), lambda _e: sel["row"])
    test._callee("subx", 0x45AFC0, 0, (), lambda _e: sel["sx"])
    test._callee("suby", 0x45AFD0, 0, (), lambda _e: sel["sy"])
    test._callee("posx", 0x620650, 1, (), lambda _e: sel["px"])
    test._callee("posy", 0x6206B0, 1, (), lambda _e: sel["py"])
    test._callee("pmode", 0x45AE90, 0, (), lambda _e: sel["pmode"])
    test._callee("onscreen", 0x4709A0, 3, ("ecx", "edx"), lambda _e: sel["onscreen"])
    test._callee("colour", 0x46ED50, 0, (), lambda _e: sel["colour"])
    test._callee("al", 0x620100, 1, (), lambda _e: sel["al"])
    test._callee("offs", 0x621810, 3, (), offs, nlog=1)
    test._callee("request", 0x4DBB50, 6, ("ecx", "edx"), request, nlog=6)
    test.callee("draw", 0x4F6570, 4, ())

    def build(img, rng):
        """Random unit state and screen numbers."""
        edge = (0, 1, -1, 3, 100, 0x7FFFFFFF, -0x80000000, 7)
        sel.update(type=rng.choice((0, 1, 2, 2)), cls=rng.randrange(1 << 32), mode=rng.randrange(8),
                   persp=rng.choice((0, 1, 0x1234, 0xFFFFFF00, M32)), row=objrow if rng.randrange(8) else 0,
                   sx=rng.choice(edge) & M32, sy=rng.choice(edge) & M32, px=rng.choice(edge) & M32, py=rng.choice(edge) & M32,
                   pmode=rng.choice((0, 1, 2, 3)), onscreen=rng.choice((0, 1, 1)), colour=rng.randrange(1 << 32),
                   al=rng.randrange(256), v1c=rng.randrange(1 << 32), ok=rng.choice((0, 1, 1)))
        o = objrow - test.arena
        img[o + 0x150] = rng.choice((0, 1, 1))
        for i in range(8):
            img[o + 0x118 + i] = rng.choice((0, 1))
        img[o + 0x148:o + 0x150] = rng.randbytes(8)
        test.m.w32(0x71146C, rng.choice((0, 7, 800, 1, -5 & M32, 0x7FFFFFFF, 4)))
        return {"ecx": unit if rng.randrange(8) else 0}

    def model(m, args):
        """The silhouette draw."""
        who = args["ecx"]
        kind, cls = (m.r(who), m.r(who + 4)) if who else (6, M32)
        m.call("mode", who)
        m.call("transform", who)
        rig.require(sel["site"] == (FILE, 0x9B6), "transform site")
        rig.require(sel["seen"] == (kind, cls), "transform locals")
        persp = m.call("persp")
        ox = oy = 0
        if sel["type"] == 2:
            row = m.call("objects", sel["cls"])
            if not row:
                raise rig.Halt(0x9BE)
            if not m.r(row + 0x150, 1) or not m.r(row + sel["mode"] + (0x119 if "mode" in MUT else 0x118), 1):
                return 0
            ox, oy = m.r(row + 0x148), m.r(row + 0x14C)
        sx = m.call("subx")
        ex = (ox - sx) & M32
        px = m.call("posx", who)
        x = (px + ex) & M32
        sy = m.call("suby")
        ey = (oy - sy) & M32
        py = m.call("posy", who)
        y = (py + ey) & M32
        if persp:
            side = m.call("pmode")
            width = test.m.r32(0x71146C)
            quarter = (s32(width) + ((s32(width) >> 31) & 3)) >> 2
            if side == 1:
                x = (x + quarter if "sign" in MUT else x - quarter) & M32
            elif side == 2:
                x = (x + quarter) & M32
        if not m.call("onscreen", who, x, y, 0, 0):
            return 0
        low = m.call("colour") & 0xFF
        al = m.call("al", who)
        m.call("offs", who)
        if not m.call("request", sel["req"], who, 0, al & 0xFF, sel["v1c"], sel["p24"], 1, M32):
            return 0
        tail = low if "tail" in MUT else (persp & 0xFFFFFF00) | low
        m.call("draw", sel["req"], x, y, tail)
        return 1
    test.run("FUN_00471a30", 0x471A30, build, model, 0)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 1500
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("mode", "sign", "tail")}
    rig.run_suite(suite, mutations, "ENGINE unit shape", args.mutation_check)


if __name__ == "__main__":
    main()
