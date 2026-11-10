# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the unit selection outline draw FUN_00471bd0 in stock 1.14d.

Run: uv run python engine_unit_outline.py [--mutation-check]
FUN_00471bd0 (ECX unit) draws the coloured outline of a hovered unit. A null
unit or a type outside 0..2 (signed) hands over to FUN_00471a30 and returns 1.
The screen position comes from the perspective path (FUN_004f51d0 set: three
conversions, a quarter-screen shift for the two side-panel modes, 16 down) or
the plain position getters; FUN_004709a0 must pass. The type / class / mode
are transformed (line 0xa2f), the COF node of the weapon token must exist
(loaded by FUN_0046fa80 when missing), its data (FUN_0046e570 at the
transformed mode), header and layer order (FUN_004db2e0, FUN_004db110; a
missing order is fatal 0xa56) give the layer list and FUN_004dbd30 its height
(0 or less ends with 0). Each layer the unit shows (FUN_004db670) is drawn
(FUN_004dbb50, FUN_004f6570) with the FUN_0046ed50 colour. The last draw
argument is the whole stack dword whose low byte is that colour; the rest is
stack residue, so only the byte is compared. Every callee is hooked. The
claim: the early exits, both position paths, the call order and arguments,
the per-layer loop and the result.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import M32, s32
from unicorn.x86_const import UC_X86_REG_ECX

MUT = set()
FILE = 0x6D6C1C


def suite(test):
    """Run FUN_00471bd0."""
    sel = {}
    unit, owner, hdr, layers = test.slot(0), test.slot(1), test.slot(2), test.slot(3)

    def convert(e):
        """FUN_00620410: write this case's two offsets through its output pointers."""
        e.w32(e.arg(2), sel["a"])
        e.w32(e.arg(3), sel["b"])
        return sel["r1"]

    def transform(e):
        """Note the site and the locals, write this case's type / class / mode."""
        sel["site"] = (e.arg(4), e.arg(5))
        sel["seen"] = (e.r32(e.arg(1)), e.r32(e.arg(2)), e.r32(e.arg(3)))
        e.w32(e.arg(1), sel["type"])
        e.w32(e.arg(2), sel["cls"])
        e.w32(e.arg(3), sel["mode"])
        return 0

    def offs(e):
        """FUN_00621810: write the value the request uses."""
        e.w32(e.arg(1), sel["v14"])
        return 0

    def token(e):
        """FUN_0064f380: note the output pointer, give this case's token."""
        sel["buf"] = e.arg(2)
        return sel["tok"]

    def param(e):
        """FUN_004db2e0: note the output pointers, write the direction and frame."""
        sel["pa"], sel["pb"], sel["pc"] = e.arg(2), e.arg(3), e.arg(4)
        if sel["w10"]:
            e.w32(e.arg(2), sel["v10"])
        e.w32(e.arg(3), sel["v28"])
        return 0

    def height(e):
        """FUN_004dbd30: note the height pointer."""
        sel["p2c"] = e.arg(1)
        return sel["height"]

    def request(e):
        """FUN_004dbb50: note the buffers, give the next outcome."""
        sel["req"] = e.mu.reg_read(UC_X86_REG_ECX)
        sel["p3c"] = e.arg(3)
        return sel["ok"].pop(0)

    def draw(e):
        """FUN_004f6570: note the last argument (the colour dword)."""
        sel["draws"].append(e.arg(3))
        return 0

    test._callee("persp", 0x4F51D0, 0, (), lambda _e: sel["persp"])
    test._callee("shape", 0x471A30, 0, ("ecx",))
    test._callee("d0f0", 0x4DA0F0, 1, ())
    test._callee("convert", 0x620410, 4, (), convert, nlog=2)
    test._callee("convert2", 0x6203B0, 2, ())
    test._callee("apply", 0x4F6760, 1, ())
    test._callee("pmode", 0x45AE90, 0, (), lambda _e: sel["pmode"])
    test._callee("d0b0", 0x4DA0B0, 1, ())
    test._callee("d0d0", 0x4DA0D0, 1, ())
    test._callee("posx", 0x620650, 1, ())
    test._callee("subx", 0x45AFC0, 0, ())
    test._callee("posy", 0x6206B0, 1, ())
    test._callee("suby", 0x45AFD0, 0, ())
    test._callee("onscreen", 0x4709A0, 3, ("ecx", "edx"), lambda _e: sel["onscreen"])
    test._callee("colour", 0x46ED50, 0, (), lambda _e: sel["colour"])
    test._callee("mode", 0x621190, 1, (), lambda _e: 0x33)
    test._callee("transform", 0x645270, 6, (), transform, nlog=1)
    test._callee("al", 0x620100, 1, (), lambda _e: sel["al"])
    test._callee("offs", 0x621810, 3, (), offs, nlog=1)
    test._callee("hastate", 0x63A7B0, 2, (), lambda _e: sel["hastate"])
    test._callee("owner", 0x4639D0, 0, ("ecx",), lambda _e: sel["owner"])
    test._callee("token", 0x64F380, 5, (), token, nlog=5)
    test._callee("node", 0x46E5A0, 1, ("ecx", "edx"), lambda _e: sel["node"])
    test._callee("load", 0x46FA80, 1, ("ecx", "edx"), lambda _e: sel["load"])
    test._callee("data", 0x46E570, 0, ("ecx", "edx"), lambda _e: sel["data"])
    test._callee("header", 0x4DB040, 0, ("ecx",), lambda _e: hdr)
    test._callee("param", 0x4DB2E0, 7, ("ecx", "edx"), param, nlog=7)
    test._callee("order", 0x4DB110, 1, ("ecx", "edx"), lambda _e: sel["order"])
    test._callee("height", 0x4DBD30, 4, ("ecx", "edx"), height, nlog=4)
    test._callee("layer", 0x4DB670, 0, ("ecx", "edx"), lambda _e: sel["shown"].pop(0))
    test._callee("request", 0x4DBB50, 6, ("ecx", "edx"), request, nlog=6)
    test._callee("draw", 0x4F6570, 4, (), draw, nlog=3)

    def build(img, rng):
        """Random unit, lookups and layer list."""
        edge = (0, 1, -1, 3, 100, 0x7FFFFFFF, -0x80000000, 7)
        sel.update(persp=rng.choice((0, 1, 1)), a=rng.choice(edge) & M32, b=rng.choice(edge) & M32, r1=rng.randrange(1 << 32),
                   pmode=rng.choice((0, 1, 2, 3)), onscreen=rng.choice((0, 1, 1, 1)), colour=rng.randrange(1 << 32),
                   al=rng.randrange(256), v14=rng.randrange(1 << 32), type=rng.randrange(4), cls=rng.randrange(1 << 32),
                   mode=rng.randrange(8), hastate=rng.randrange(2), owner=owner if rng.randrange(2) else 0,
                   tok=rng.randrange(1 << 32), node=rng.choice((0, 1, 1)), load=rng.choice((0, 1, 1)),
                   data=rng.choice((0, 5, 0x1234, 0x1234)), w10=rng.randrange(2), v10=rng.randrange(1 << 32),
                   v28=rng.randrange(1 << 32), order=layers if rng.randrange(10) else 0,
                   height=rng.choice((0, 1, 5, M32, 0x80000000, 0x7FFFFFFF)), draws=[])
        sel["ok"] = [rng.choice((0, 1, 1)) for _ in range(6)]
        sel["shown"] = [rng.choice((0, 1, 1)) for _ in range(6)]
        o = unit - test.arena
        img[o:o + 4] = rng.choice((0, 1, 2, 0, 1, 2, 3, M32, 0x80000000, 5)).to_bytes(4, "little")
        img[o + 0xC8:o + 0xCC] = rng.choice((0, 8)).to_bytes(4, "little")
        h = hdr - test.arena
        img[h] = rng.choice((0, 1, 2, 3, 4, 5))
        test.m.w32(0x71146C, rng.choice((0, 7, 800, 1, -5 & M32, 0x7FFFFFFF, 4)))
        return {"ecx": unit if rng.randrange(10) else 0}

    def model(m, args):
        """The outline draw."""
        who = args["ecx"]
        persp = m.call("persp")
        if not who or s32(m.r(who)) < 0 or s32(m.r(who)) > (1 if "type" in MUT else 2):
            m.call("shape", who)
            return 1
        if persp:
            d = m.call("d0f0", who)
            r1 = m.call("convert", who, d)
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
            x = (x + m.call("d0b0", who)) & M32
            y = (y + m.call("d0d0", who)) & M32
        else:
            px = m.call("posx", who)
            x = (px - m.call("subx")) & M32
            x = (x + m.call("d0b0", who)) & M32
            py = m.call("posy", who)
            y = (py - m.call("suby")) & M32
            y = (y + m.call("d0d0", who)) & M32
            y = (y + m.call("d0f0", who)) & M32
        if not m.call("onscreen", who, x, y, 0, 0):
            return 0
        pal = m.call("colour") & 0xFF
        kind, cls = m.r(who), m.r(who + 4)
        m.call("mode", who)
        m.call("transform", who)
        rig.require(sel["site"] == (FILE, 0xA2F), "transform site")
        rig.require(sel["seen"] == (kind, cls, 0x33), "transform locals")
        al = m.call("al", who) & 0xFF
        m.call("offs", who)
        src = m.r(who + 0x60)
        if (m.r(who + 0xC8) >> 3) & 1 and m.call("hastate", who, 0x25):
            found = m.call("owner", who)
            if found:
                src = m.r(found + 0x60)
        tok = m.call("token", who, src, sel["buf"], sel["mode"], 0)
        if not m.call("node", who, tok, M32) and not m.call("load", who, tok, M32):
            return 0
        data = m.call("data", who, 0x33 if "mode" in MUT else sel["mode"])
        if not data:
            return 0
        header = m.call("header", data)
        count = m.r(header, 1)
        m.call("param", who, sel["type"], sel["cls"], sel["mode"], sel["pa"], sel["pb"], sel["pc"], m.r(header + 2, 1), 1)
        direction = sel["v10"] if sel["w10"] else al
        order = m.call("order", data, sel["v28"], sel["v14"])
        if not order:
            raise rig.Halt(0xA56)
        if s32(m.call("height", who, header, order, sel["p2c"], direction, sel["v14"])) <= 0:
            return 0
        for i in range(count - 1 if "skip" in MUT else count):
            layer = m.r(order + i, 1)
            if m.call("layer", who, layer) and m.call("request", sel["req"], who, 0, direction, sel["v14"], sel["p3c"], layer, M32):
                m.call("draw", sel["req"], x, y)
        rig.require(all(c & 0xFF == (pal ^ 1 if "pal" in MUT else pal) for c in sel["draws"]), f"draw colour bytes {sel['draws']} vs {pal:#x}")
        return 1
    test.run("FUN_00471bd0", 0x471BD0, build, model, 0)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 2000
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("type", "mode", "skip", "pal")}
    rig.run_suite(suite, mutations, "ENGINE unit outline", args.mutation_check)


if __name__ == "__main__":
    main()
