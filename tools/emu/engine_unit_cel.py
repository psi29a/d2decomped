# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the unit cel draw FUN_00471450 in stock 1.14d.

Run: uv run python engine_unit_cel.py [--mutation-check]
FUN_00471450 (ECX unit) draws a unit's cel in one call: it refuses a unit
whose +0xc4 bit 5 is set or that has state 0x92, transforms the type / class /
mode (line 0x86f), takes an object's draw offset (+0x148 / +0x14c, only with
Objects.txt +0x150 and +0x118 + mode set; a missing row is fatal 0x87a),
adds the position getters, the screen offsets (shifting a quarter screen for
the two side-panel modes and 16 down when FUN_004f51d0 is set), asks
FUN_004709a0 whether the box is on screen, fills a request
(FUN_004dbb50 after FUN_00620100 / FUN_00621810) and draws it
(FUN_004f6540). It returns 1 when drawn. Every callee is hooked. The claim:
the early exits, the arithmetic, the argument lists and the result.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import M32, s32
from unicorn.x86_const import UC_X86_REG_ECX

MUT = set()
FILE = 0x6D6C1C


def suite(test):
    """Run FUN_00471450."""
    sel = {}
    unit, objrow = test.slot(0), test.slot(1)

    def transform(e):
        """Note the site, write this case's type / class / mode."""
        sel["site"] = (e.arg(4), e.arg(5))
        sel["seen"] = (e.r32(e.arg(1)), e.r32(e.arg(2)))
        e.w32(e.arg(1), sel["type"])
        e.w32(e.arg(2), sel["cls"])
        e.w32(e.arg(3), sel["mode"])
        return 0

    def offs(e):
        """FUN_00621810: write the offset the request uses."""
        sel["p1c"] = e.arg(1)
        e.w32(e.arg(1), sel["v1c"])
        return 0

    def request(e):
        """FUN_004dbb50: note the buffers."""
        sel["req"] = e.mu.reg_read(UC_X86_REG_ECX)
        sel["p24"] = e.arg(3)
        return sel["ok"]

    test._callee("hastate", 0x639DF0, 2, (), lambda _e: sel["hastate"])
    test._callee("mode", 0x621190, 1, (), lambda _e: 0x33)
    test._callee("transform", 0x645270, 6, (), transform, nlog=1)
    test._callee("automap", 0x4F51D0, 0, (), lambda _e: sel["automap"])
    test._callee("objects", 0x640E90, 1, (), lambda _e: sel["row"])
    test._callee("subx", 0x45AFC0, 0, (), lambda _e: sel["sx"])
    test._callee("suby", 0x45AFD0, 0, (), lambda _e: sel["sy"])
    test._callee("posx", 0x620650, 1, (), lambda _e: sel["px"])
    test._callee("posy", 0x6206B0, 1, (), lambda _e: sel["py"])
    test._callee("motx", 0x4DA110, 1, (), lambda _e: sel["mx"])
    test._callee("moty", 0x4DA130, 1, (), lambda _e: sel["my"])
    test._callee("pmode", 0x45AE90, 0, (), lambda _e: sel["pmode"])
    test._callee("onscreen", 0x4709A0, 3, ("ecx", "edx"), lambda _e: sel["onscreen"])
    test._callee("al", 0x620100, 1, (), lambda _e: sel["al"])
    test._callee("offs", 0x621810, 3, (), offs, nlog=1)
    test._callee("request", 0x4DBB50, 6, ("ecx", "edx"), request, nlog=6)
    test.callee("draw", 0x4F6540, 3, ())

    def build(img, rng):
        """Random unit state and screen numbers."""
        edge = (0, 1, -1, 3, 100, 0x7FFFFFFF, -0x80000000, 7)
        sel.update(hastate=rng.choice((0, 0, 0, 1)), type=rng.choice((0, 1, 2, 2)), cls=rng.randrange(1 << 32),
                   mode=rng.randrange(8), automap=rng.randrange(2), row=objrow if rng.randrange(8) else 0,
                   sx=rng.choice(edge) & M32, sy=rng.choice(edge) & M32, px=rng.choice(edge) & M32, py=rng.choice(edge) & M32,
                   mx=rng.choice(edge) & M32, my=rng.choice(edge) & M32, pmode=rng.choice((0, 1, 2, 3)),
                   onscreen=rng.choice((0, 1, 1)), al=rng.randrange(256), v1c=rng.randrange(1 << 32), ok=rng.choice((0, 1, 1)))
        img[unit - test.arena + 0xC4:unit - test.arena + 0xC8] = rng.choice((0, 0, 0x20, 0xFFFFFFFF)).to_bytes(4, "little")
        o = objrow - test.arena
        img[o + 0x150] = rng.choice((0, 1, 1))
        for i in range(8):
            img[o + 0x118 + i] = rng.choice((0, 1))
        img[o + 0x148:o + 0x150] = rng.randbytes(8)
        test.m.w32(0x71146C, rng.choice((0, 7, 800, 1, -5 & M32, 0x7FFFFFFF, 4)))
        return {"ecx": unit}

    def model(m, args):
        """The cel draw."""
        if (m.r(unit + 0xC4) >> 5) & 1:
            return 0
        if m.call("hastate", unit, 0x92):
            return 0
        m.call("mode", unit)
        m.call("transform", unit)
        rig.require(sel["site"] == (FILE, 0x86F), "transform site")
        rig.require(sel["seen"] == (m.r(unit), m.r(unit + 4)), "transform locals")
        flag = m.call("automap")
        ox = oy = 0
        if sel["type"] == 2:
            row = m.call("objects", sel["cls"])
            if not row:
                raise rig.Halt(0x87A)
            if not m.r(row + 0x150, 1) or not m.r(row + sel["mode"] + (0x119 if "mode" in MUT else 0x118), 1):
                return 0
            ox, oy = m.r(row + 0x148), m.r(row + 0x14C)
        sx = m.call("subx")
        ex = (ox - sx) & M32
        px = m.call("posx", unit)
        t = (px + ex) & M32
        mx = m.call("motx", unit)
        x = (mx + t) & M32
        sy = m.call("suby")
        ey = (oy - sy) & M32
        py = m.call("posy", unit)
        t = (py + ey) & M32
        my = m.call("moty", unit)
        y = (my + t) & M32
        if flag:
            kind = m.call("pmode")
            width = test.m.r32(0x71146C)
            quarter = (s32(width) + ((s32(width) >> 31) & 3)) >> 2
            if kind == 1:
                x = (x + quarter if "sign" in MUT else x - quarter) & M32
            elif kind == 2:
                x = (x + quarter) & M32
            y = (y + (0x11 if "y" in MUT else 0x10)) & M32
        if not m.call("onscreen", unit, x, y, 0, 1):
            return 0
        al = m.call("al", unit)
        m.call("offs", unit)
        if not m.call("request", sel["req"], unit, 0, al, sel["v1c"], sel["p24"], 1, M32):
            return 0
        m.call("draw", sel["req"], x, y)
        return 1
    test.run("FUN_00471450", 0x471450, build, model, 0)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 1500
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("y", "sign", "mode")}
    rig.run_suite(suite, mutations, "ENGINE unit cel", args.mutation_check)


if __name__ == "__main__":
    main()
