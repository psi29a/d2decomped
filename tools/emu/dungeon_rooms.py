# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the room1 geometry, search, near-array and creation functions of DUNGEON\\Dungeon.cpp in stock game.exe 1.14d.

    uv run python dungeon_rooms.py [--mutation-check]

Runs game.exe's own code for FUN_00619800 (build a room1's near-room array),
FUN_00619890 (create a room1), FUN_00619970 (rectangle overlap),
FUN_006199f0 (point in rectangle), FUN_00619a50 (rectangle shape code),
FUN_00619b10 (room1 containing a point, on a dungeon's list),
FUN_00619b60 (near room containing a point), FUN_00619bd0 (callback walk over
the near rooms) and FUN_00619d00 (rectangle export) against a hand-written
model on random room1 records (_dungeon_rig.py): return value, callee log,
the whole arena, the blocks the arena allocator handed out, the fatal-error
line and the bytes `ret n` pops. FUN_00619890 runs FUN_00619800 for real.
Hooked, logged, not claimed: the room2 helpers 0x66_bcf0, 0x66_bd00,
0x66_bfc0, 0x65_0e40, 0x64_c900, the allocator, two callbacks. The CRT memset
runs for real. `--mutation-check` breaks the model four ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt, s32

MUT = set()
CB_STD, CB_THIS = 0x40_2010, 0x40_2020      # unused code, hooked as callbacks


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def put(img, rig, slot, off, value):
    """Write a dword into slot `slot` of the arena image."""
    index = slot * 0x100 + off
    img[index:index + 4] = (value & M32).to_bytes(4, "little")


def pick(rng, address, null=0.25):
    """`address` or, sometimes, null."""
    return 0 if rng.random() < null else address


def small(rng):
    """A coordinate near zero, or an extreme."""
    return rng.choice((rng.randrange(-4, 8), rng.randrange(-4, 8), rng.randrange(-4, 8), 0x7FFFFFFF, 0x80000000,
                       0xFFFFFFFF, 0x7FFFFFFE)) & M32


def shape(img, rig, rng, slot):
    """Fill a room1 slot's rectangle (+0x5c x, +0x60 y, +0x64 w, +0x68 h): mostly -2..3 so edges meet, sometimes extreme."""
    for off in (0x5C, 0x60, 0x64, 0x68):
        put(img, rig, slot, off, rng.randrange(-2, 4) if rng.random() < 0.8 else small(rng))


def inside(m, r, x, y):
    """The point test of FUN_006199f0 on the model: lower bound inclusive, upper exclusive, signed."""
    rx, ry, w, h = (m.r(r + o) for o in (0x5C, 0x60, 0x64, 0x68))
    if s32(x) < s32(rx) or s32(x) >= s32(w + rx):
        return 0
    if s32(y) < s32(ry) or s32(y) >= s32(h + ry):
        return 0
    return 1


class Suite:
    """Builds and runs the room1 cases."""

    def __init__(self, rig):
        """Hook the callees this oracle leaves unclaimed."""
        self.rig = rig
        rig.callee("cb_std", CB_STD, 2, ())
        rig.callee("cb_this", CB_THIS, 0, ("ecx",))
        for name, addr, nstack, regs in (("c66bcf0", 0x66_BCF0, 0, ("ecx",)), ("c66bd00", 0x66_BD00, 0, ("ecx", "edx")),
                                         ("c66bfc0", 0x66_BFC0, 0, ("ecx", "edx")),
                                         ("c650e40", 0x65_0E40, 0, ("ecx", "edx")),
                                         ("c64c900", 0x64_C900, 0, ("ecx", "edx"))):
            rig.callee(name, addr, nstack, regs)
        rig.n_val, rig.fill, rig.fill_pending, rig.cb_vals = 0, [], False, []
        rig.custom["c66bcf0"] = lambda e: rig.n_val
        rig.custom["c66bd00"] = self.near_hook
        rig.custom["cb_std"] = rig.custom["cb_this"] = lambda e: rig.cb_vals.pop(0) if rig.cb_vals else 1

    def near_hook(self, e):
        """0x66_bd00 stand-in: the first call fills the array at edx with rig.fill and returns its length."""
        rig = self.rig
        if rig.fill_pending:
            rig.fill_pending = False
            from unicorn.x86_const import UC_X86_REG_EDX
            base = e.mu.reg_read(UC_X86_REG_EDX)
            for i, v in enumerate(rig.fill):
                e.w32(base + 4 * i, v)
            return len(rig.fill)
        return 0x1234

    # ---- pure geometry ----------------------------------------------------------------
    def geometry(self):
        """FUN_00619970, FUN_006199f0, FUN_00619a50, FUN_00619d00."""
        rig = self.rig

        def b_overlap(img, rng):
            """Two room1s with small rectangles, either may be null."""
            shape(img, rig, rng, 0)
            shape(img, rig, rng, 1)
            return {"stack": [pick(rng, rig.slot(0), 0.15), pick(rng, rig.slot(1), 0.15)]}

        def m_overlap(m, a):
            """Fatal 0x13e / 0x141 on a null; else the x ranges and then the y ranges must overlap (signed)."""
            ra, rb = a["stack"]
            if ra == 0:
                raise Halt(0x13E)
            if rb == 0:
                raise Halt(0x141)
            ax, ay, aw, ah = (m.r(ra + o) for o in (0x5C, 0x60, 0x64, 0x68))
            bx, by, bw, bh = (m.r(rb + o) for o in (0x5C, 0x60, 0x64, 0x68))
            if s32(ax) > s32(bw + bx) or s32(aw + ax) < s32(bx):
                return 0
            if s32(ay) > s32(bh + by):
                return 0
            return int(s32(ah + ay) >= s32(by) + (1 if mut("overlap-strict") else 0))
        rig.run("FUN_00619970", 0x619970, b_overlap, m_overlap, 2)

        def b_point(img, rng):
            """A room1 (sometimes null), a point inside or near the rectangle."""
            shape(img, rig, rng, 0)
            return {"stack": [pick(rng, rig.slot(0), 0.15), rng.randrange(-3, 5) & M32, rng.randrange(-3, 5) & M32]}

        def m_point(m, a):
            """Fatal 0x168 on null; else the signed half-open rectangle test."""
            r, x, y = a["stack"]
            if r == 0:
                raise Halt(0x168)
            return inside(m, r, x, y)
        rig.run("FUN_006199f0", 0x6199F0, b_point, m_point, 3)

        def b_shape(img, rng):
            """A rectangle with a zero or negative extent often."""
            shape(img, rig, rng, 0)
            if rng.random() < 0.4:
                put(img, rig, 0, 0x64, 0)
            if rng.random() < 0.4:
                put(img, rig, 0, 0x68, 0)
            return {"stack": [pick(rng, rig.slot(0), 0.15), rng.randrange(1 << 32)]}   # `ret 8`: a second, unused dword

        def m_shape(m, a):
            """0 for a negative width or height; 1 for width 0; else 3 for height 0, 4 otherwise. Fatal 0x17b on null."""
            r = a["stack"][0]
            if r == 0:
                raise Halt(0x17B)
            x, y, w, h = (m.r(r + o) for o in (0x5C, 0x60, 0x64, 0x68))
            if s32(x) > s32(x + w) or s32(w + x) < s32(x):
                return 0
            if s32(y) > s32(h + y) or s32(h + y) < s32(y):
                return 0
            if x == (x + w) & M32:
                return 1
            return 3 + int(y != (y + h) & M32) + (1 if mut("shape-code") else 0)
        rig.run("FUN_00619a50", 0x619A50, b_shape, m_shape, 2)

        def b_export(img, rng):
            """A room1 with small tile rectangle at +0x4c..+0x58; out array in slot 3."""
            for off in (0x4C, 0x50, 0x54, 0x58):
                put(img, rig, 0, off, small(rng))
            return {"stack": [pick(rng, rig.slot(0), 0.15), rig.slot(3)]}

        def m_export(m, a):
            """out = {x, y, x+w, y+h} written first, then fatal 0x1eb (y > y+h) or 0x1ec (x > x+w)."""
            r, out = a["stack"]
            if r == 0:
                raise Halt(0x1E3)
            x, y, w, h = (m.r(r + o) for o in (0x4C, 0x50, 0x54, 0x58))
            m.w(out + 4, y)
            m.w(out + 0xC, h + y)
            m.w(out, x)
            m.w(out + 8, w + x)
            if s32(y) > s32(h + y):
                raise Halt(0x1EB)
            if s32(x) > s32((w + x) & M32 if not mut("export-x") else y):
                raise Halt(0x1EC)
        rig.run("FUN_00619d00", 0x619D00, b_export, m_export, 2, ret=False)

    # ---- searches ---------------------------------------------------------------------
    def searches(self):
        """FUN_00619b10, FUN_00619b60, FUN_00619bd0."""
        rig = self.rig

        def b_chain(img, rng):
            """A dungeon whose +0x10 starts a chain of three room1s (next +0x7c) with small rectangles."""
            slots = [4, 5, 6]
            put(img, rig, 0, 0x10, rig.slot(slots[0]) if rng.random() < 0.9 else 0)
            for i, s in enumerate(slots):
                shape(img, rig, rng, s)
                put(img, rig, s, 0x7C, rig.slot(slots[i + 1]) if i + 1 < len(slots) else 0)
            return {"stack": [rig.slot(0), small(rng), small(rng)]}

        def m_chain(m, a):
            """First room1 on the chain whose rectangle contains (x, y), or 0."""
            d, x, y = a["stack"]
            node = m.r(d + 0x10)
            while node:
                if inside(m, node, x, y):
                    return node
                node = m.r(node + 0x7C)
            return 0
        rig.run("FUN_00619b10", 0x619B10, b_chain, m_chain, 3)

        def b_near(img, rng):
            """A room1 whose near array (+0, count at +0x24) holds up to four room1s, one possibly null."""
            count = rng.randrange(0, 5)
            put(img, rig, 0, 0x24, count)
            put(img, rig, 0, 0, rig.slot(3))
            for i in range(count):
                s = 4 + i
                shape(img, rig, rng, s)
                put(img, rig, 3, 4 * i, 0 if rng.random() < 0.1 else rig.slot(s))
            return {"stack": [pick(rng, rig.slot(0), 0.15), small(rng), small(rng)]}

        def m_near(m, a):
            """Fatal 0x1af on null; first near room containing the point; a null entry is fatal 0x1b3."""
            r, x, y = a["stack"]
            if r == 0:
                raise Halt(0x1AF)
            array = m.r(r)
            for i in range(m.r(r + 0x24)):
                s = m.r(array + 4 * i)
                if s == 0:
                    raise Halt(0x1B3)
                if inside(m, s, x, y):
                    return s
            return 0
        rig.run("FUN_00619b60", 0x619B60, b_near, m_near, 3)

        def b_walk(img, rng):
            """A room1 with up to four near entries; the callback address and the bad-pointer answer vary."""
            count = rng.randrange(0, 5)
            put(img, rig, 0, 0x24, count)
            put(img, rig, 0, 0, rig.slot(3))
            for i in range(count):
                put(img, rig, 3, 4 * i, rng.randrange(1 << 32))
            rig.bad_code = rng.choice((0, 0, 0, 1))
            rig.cb_vals = [rng.choice((0, 1, 5, 0x80000000)) for _ in range(6)]
            return {"stack": [pick(rng, rig.slot(0), 0.1), CB_STD, rng.randrange(1 << 32)]}

        def m_walk(m, a):
            """Fatal 0x1c0 (null), 0x1c2 (IsBadCodePtr); else call cb(near, arg) until it returns 0."""
            r, cb, arg = a["stack"]
            if r == 0:
                raise Halt(0x1C0)
            if rig.bad_code:
                raise Halt(0x1C2)
            result = 0
            array = m.r(r)
            for i in range(m.r(r + 0x24)):
                result = m.call("cb_std", m.r(array + 4 * i), arg)
                if result == 0 and not mut("walk-continue"):
                    break
            return result
        rig.run("FUN_00619bd0", 0x619BD0, b_walk, m_walk, 3)

    # ---- construction -----------------------------------------------------------------
    def near_array(self, m, room, pool):
        """The model of FUN_00619800 on `room` (ESI) with the pool argument; fatal 0x108 on a null entry."""
        rig = self.rig
        room2 = m.r(room + 0x10)
        n = m.call("c66bcf0", room2)
        ptr = m.alloc(pool, n * 4, 0x100)
        m.w(room, ptr)
        for i, v in enumerate(rig.fill):
            m.w(ptr + 4 * i, v)
        count = m.call("c66bd00", room2, ptr)
        m.w(room + 0x24, count)
        for i in range(count):
            e = m.r(ptr + 4 * i)
            if e == 0:
                raise Halt(0x108)
            if e != room:
                m.w(e + 0x24, m.call("c66bd00", m.r(e + 0x10), m.r(e)))

    def setup_near(self, img, rng, room_slots):
        """Choose the near count and fill for a case; the entries are the given room slots, maybe self or null."""
        rig = self.rig
        n = rng.randrange(0, 5)
        rig.n_val = n
        rig.fill = [rig.slot(rng.choice(room_slots)) if rng.random() > 0.08 else 0 for _ in range(rng.randrange(0, n + 1))]
        rig.fill_pending = True

    def construction(self):
        """FUN_00619800 and FUN_00619890."""
        rig = self.rig

        def b_near_array(img, rng):
            """Room1 in slot 0 (self may be in its own near list), other rooms in slots 1..3."""
            self.setup_near(img, rng, (0, 1, 2, 3))
            for s in (0, 1, 2, 3):
                put(img, rig, s, 0x10, rng.randrange(1 << 32))
            return {"regs": {"esi": rig.slot(0)}, "stack": [rng.randrange(1 << 32)]}

        def m_near_array(m, a):
            """Build the near array of ESI with the pool in the stack argument."""
            self.near_array(m, a["regs"]["esi"], a["stack"][0])
        rig.run("FUN_00619800", 0x619800, b_near_array, m_near_array, 1, ret=False)

        def b_create(img, rng):
            """Dungeon in slot 0 (may be null), room2 value, rectangle in slot 8, near rooms in slots 1..3."""
            self.setup_near(img, rng, (1, 2, 3))
            put(img, rig, 0, 0x4C, rig.slot(0) and (CB_THIS if rng.random() < 0.6 else 0))
            for off in range(0, 32, 4):
                put(img, rig, 8, off, rng.randrange(1 << 32))
            for s in (1, 2, 3):
                put(img, rig, s, 0x10, rng.randrange(1 << 32))
            return {"ecx": pick(rng, rig.slot(0), 0.1), "edx": rng.randrange(1 << 32),
                    "stack": [rig.slot(8), rng.randrange(1 << 32), rng.randrange(1 << 32), rng.randrange(1 << 32)]}

        def m_create(m, a):
            """Allocate 0x80 bytes, fill the room1, link it as the dungeon's list head, build its near array."""
            d, room2 = a["ecx"], a["edx"]
            rect, f8, f6c, f34 = a["stack"]
            if d == 0:
                raise Halt(0x119)
            pool = m.r(d + 0x5C)
            room = m.alloc(pool, 0x80, 0xF5)
            m.w(room + 0x34, f34)
            m.call("c650e40", room + 0x6C, f6c)
            m.w(room + 0x10, room2)
            for off in range(0, 32, 4):
                m.w(room + 0x4C + off, m.r(rect + off))
            m.w(room + 8, f8)
            m.w(room + 0x7C, m.r(d + 0x10))
            m.w(d + 0x10, room)
            m.w(d + 0x54, 1)
            m.w(room + 0x2C, d)
            m.call("c66bfc0", room2, room)
            self.near_array(m, room, m.r(d + 0x5C))
            m.call("c64c900", m.r(d + 0x5C), room)
            callback = m.r(d + 0x4C)
            if callback:
                m.call("cb_this", room)
            return room
        rig.run("FUN_00619890", 0x619890, b_create, m_create, 4)


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 80
    s = Suite(rig)
    s.geometry()
    s.searches()
    s.construction()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("overlap-strict", "shape-code", "export-x", "walk-continue")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "Dungeon.cpp room1 geometry, search and creation", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
