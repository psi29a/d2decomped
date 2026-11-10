# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the overlay row draw FUN_0046e1b0 in stock 1.14d.

Run: uv run python engine_overlay_draw.py [--mutation-check]
FUN_0046e1b0 (ECX unit, EDX Overlay.txt row, stack: x, y) draws the unit's
overlays of that row. It walks the overlay list (+0x2c, next at +0xa4) once,
skipping nodes whose +0x34 is set, kind-6 nodes without +8 and kind-8 nodes
without +0x3c; for a node of the row it checks the row against the
Overlay.txt table (0x744304 + 0xbbc, count +0xbc0, 0x84 bytes a row; a bad
row ends the whole function), fills a cel request (FUN_00620100 then
FUN_004dbb50: node +0x18 >> 8 is the frame) and draws it with FUN_004f6480
when the frame is below the node's +0x1c >> 8, offset by +0x20 / +0x24. The
second pass the code is written for (nodes with +0x34 set) never runs: the
list pointer is null by then, so those nodes are never drawn. The graphics
record getter, FUN_00620100, FUN_004dbb50 and FUN_004f6480 are hooked. The
claim: the filters, the exits, the arithmetic, the argument lists and the
missing second pass.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import M32, s32
from unicorn.x86_const import UC_X86_REG_ECX

MUT = set()


def suite(test):
    """Run FUN_0046e1b0."""
    sel = {}
    unit, gfx = test.slot(0), test.slot(1)
    slots = (2, 3, 4)
    hdr, rows = test.arena + 0x1000, test.arena + 0x2000

    def request(e):
        """FUN_004dbb50: note the buffers, give the next outcome."""
        sel["req"] = e.mu.reg_read(UC_X86_REG_ECX)
        sel["p10"] = e.arg(3)
        return sel["ok"].pop(0)

    test._callee("gfxinfo", 0x463E10, 0, ("ecx",), lambda _e: sel["gfx"])
    test._callee("al", 0x620100, 1, (), lambda _e: sel["al"])
    test._callee("request", 0x4DBB50, 6, ("ecx", "edx"), request, nlog=6)
    test.callee("draw", 0x4F6480, 6, ())

    def build(img, rng):
        """An overlay list around the wanted row."""
        def put(addr, value):
            """Store an arena dword."""
            img[addr - test.arena:addr - test.arena + 4] = (value & M32).to_bytes(4, "little")
        sel.update(gfx=gfx if rng.randrange(8) else 0, al=rng.randrange(256), ok=[rng.choice((0, 1, 1, 1)) for _ in range(4)])
        row = rng.choice((0, 1, 2, 0xFFFFFFFF))
        count = len(slots) if rng.randrange(2) else rng.randrange(len(slots) + 1)
        used = [test.slot(i) for i in slots[:count]]
        put(gfx + 0x2C, used[0] if used else 0)
        put(hdr + 0xBBC, rows if (rng.randrange(10) or row != 0) else 0)     # a null table with row > 0 would fault; stock never has one
        put(hdr + 0xBC0, rng.choice((1, 2, 3)))
        for i, n in enumerate(used):
            put(n + 0xA4, used[i + 1] if i + 1 < len(used) else 0)
            put(n, rng.choice((6, 8, 1, 1)))
            put(n + 4, rng.choice((row, row, row, rng.randrange(3))))
            put(n + 8, rng.choice((0, 5)))
            put(n + 0x34, rng.choice((0, 0, 1, 2, 0x80)))    # any nonzero value counts: stock tests, not compares with 1
            put(n + 0x3C, rng.choice((0, 1)))
            put(n + 0x18, rng.choice((0, 0x100, 0x300, 0x80000000, 0xFFFFFF00)))
            put(n + 0x1C, rng.choice((0x100, 0x200, 0x400, 0x80000000)))
        test.m.w32(0x744304, hdr)
        return {"ecx": unit, "edx": row, "stack": [rng.randrange(1 << 32), rng.randrange(1 << 32)]}

    def model(m, args):
        """The first pass over the list; the second never runs."""
        row, (x, y) = args["edx"], args["stack"]
        info = m.call("gfxinfo", unit)
        if not info:
            return
        node = m.r(info + 0x2C)
        while node:
            flag = m.r(node + 0x34)
            skip = (flag == 1 if "eq1" in MUT else bool(flag)) and "back" not in MUT
            kind = m.r(node)
            if kind == 6 and not m.r(node + 8):
                skip = True
            if kind == 8 and not m.r(node + 0x3C):
                skip = True
            if m.r(node + 4) == row and not skip:
                frame = s32(m.r(node + 0x18)) >> (7 if "frame" in MUT else 8)
                if s32(row) < 0 or s32(row) >= s32(m.r(hdr + 0xBC0)):
                    return
                orow = m.r(hdr + 0xBBC) + row * 0x84
                if not (orow & M32):
                    return
                al = m.call("al", unit)
                if not m.call("request", sel["req"], 0, 0, al, frame & M32, sel["p10"], 1, M32):
                    return
                if frame < (s32(m.r(node + 0x1C)) >> 8):
                    m.call("draw", sel["req"], (m.r(node + 0x20) + (y if "offset" in MUT else x)) & M32, (m.r(node + 0x24) + (x if "offset" in MUT else y)) & M32, 0xFFFFFF80, m.r(orow + 0x7C, 1), 0)
            node = m.r(node + 0xA4)
    test.run("FUN_0046e1b0", 0x46E1B0, build, model, 2, ret=False)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 1500
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("back", "frame", "offset", "eq1")}
    rig.run_suite(suite, mutations, "ENGINE overlay draw", args.mutation_check)


if __name__ == "__main__":
    main()
