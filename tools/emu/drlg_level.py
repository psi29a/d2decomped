# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify Drlg.cpp's level teardown and room-at-point search in stock game.exe 1.14d.

    uv run python drlg_level.py [--mutation-check]

Runs game.exe's own code for FUN_00642010 (tear down or reset a level's
sub-structures; level in ESI, pool and flag on the stack) and FUN_00642c30
(the room holding a point: the given room, its near rooms, else the level
found or allocated for the point) against hand-written models on random
records (_dungeon_rig.py): callee log, arena, allocator blocks and the bytes
`ret n` pops. The CRT memset and FUN_0066b980 run for real, with an allocator
that returns dirty memory. Hooked, logged, not claimed: FUN_0066c100,
FUN_006683d0, FUN_00673fe0, FUN_006674d0, FUN_006754c0, FUN_006427a0,
FUN_00642ae0, FUN_006424a0, FUN_00642630 and the allocator.
`--mutation-check` breaks the model three ways (swapped y, wider flag mask, automap freed on a reset too).
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, s32  # noqa: F401

MUT = set()
FILE = 0x6E_B2E0


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def put(img, off, value):
    """Write a dword at arena offset `off`."""
    img[off:off + 4] = (value & M32).to_bytes(4, "little")


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig):
        """Hook the callees this oracle leaves unclaimed; the allocator returns dirty memory."""
        self.rig = rig
        self.base = rig.arena
        for name, addr, nstack, regs in (
                ("c66c100", 0x66_C100, 0, ("ecx",)), ("c6683d0", 0x66_83D0, 0, ("ecx", "edx")),
                ("c673fe0", 0x67_3FE0, 0, ("ecx", "edx")), ("c6674d0", 0x66_74D0, 0, ("ecx", "edx")),
                ("c6754c0", 0x67_54C0, 0, ("ecx", "edx")), ("c6427a0", 0x64_27A0, 1, ("ecx", "edx")),
                ("c642ae0", 0x64_2AE0, 0, ("ecx", "edx")), ("c6424a0", 0x64_24A0, 1, ()),
                ("c642630", 0x64_2630, 1, ("ecx", "edx"))):
            rig.callee(name, addr, nstack, regs)
        self.state = {"lid": 0}
        rig.custom["c6427a0"] = lambda e: self.state["lid"]
        rig.custom["c642ae0"] = lambda e: self.base + 0xF00
        rig.custom["c642630"] = lambda e: self.base + 0x1100

        def dirty(e):
            """Allocator stand-in returning dirty memory, so memsets are visible."""
            from unicorn.x86_const import UC_X86_REG_EDX
            size = max(e.mu.reg_read(UC_X86_REG_EDX), 1)
            p = e.alloc(size)
            e.mu.mem_write(p, b"\xAA" * size)
            return p
        rig.custom["alloc"] = dirty

    def at(self, off):
        """Arena address of offset `off`."""
        return self.base + off

    def teardown(self):
        """FUN_00642010."""
        rig, at = self.rig, self.at

        def build(img, rng):
            """A level at 0x200 (the first 0x240 bytes) with rooms, a flag array, an automap, link nodes."""
            put(img, 0x200 + 0, rng.choice((0, 1, 2, 3, 4)))
            n = rng.randrange(0, 4)
            put(img, 0x200 + 8, rng.choice((n, n, 0)))
            put(img, 0x200 + 0x22C, rng.choice((0, 0, at(0x1400))))
            put(img, 0x200 + 0x10, at(0x700) if n else 0)
            for i in range(n):
                put(img, 0x700 + 0x80 * i + 0x24, at(0x700 + 0x80 * (i + 1)) if i + 1 < n else 0)
                put(img, 0x700 + 0x80 * i + 0x60, rng.getrandbits(32))
            put(img, 0x200 + 0x1B0, rng.choice((0, at(0x900))))
            put(img, 0x200 + 0x1BC, rng.choice((0, at(0x940))))
            k = rng.randrange(0, 3)
            put(img, 0x200 + 0x1CC, at(0x980) if k else 0)
            for i in range(k):
                put(img, 0x980 + 0x40 * i + 0xC, at(0x980 + 0x40 * (i + 1)) if i + 1 < k else 0)
            return {"regs": {"esi": at(0x200)}, "stack": [rng.getrandbits(32), rng.choice((0, 1))]}

        def model(m, a):
            """Mirror of the decompiled flow; see drlg.md "Drlg.cpp act and level helpers"."""
            lv = a["regs"]["esi"]
            pool, flag = a["stack"]
            if flag:
                if m.r(lv + 0x22C) == 0:
                    n = m.r(lv + 8)
                    if n:
                        m.w(lv + 0x22C, m.alloc(pool, n * 4, 0x7C, FILE))
                    else:
                        m.w(lv + 8, 0)
                ptr, n = m.r(lv + 0x22C), m.r(lv + 8)
                for i in range(n):
                    m.w(ptr + 4 * i, 0)
            elif m.r(lv + 0x22C):
                m.free(pool, m.r(lv + 0x22C), 0x84, FILE)
                m.w(lv + 0x22C, 0)
            room = m.r(lv + 0x10)
            if room:
                off = 0
                while room:
                    nxt = m.r(room + 0x24)
                    flags = m.r(lv + 0x22C)
                    if flags:
                        m.w(flags + off, m.r(room + 0x60) & (3 if mut("flags-bit") else 1))
                        off += 4
                    m.call("c66c100", room)
                    room = nxt
                m.w(lv + 0x10, 0)
                m.w(lv + 8, 0)
            if m.r(lv + 0x1B0):
                m.call("c6683d0", pool, m.r(lv + 0x1B0))
                m.w(lv + 0x1B0, 0)
            kind = m.r(lv)
            target = {1: "c673fe0", 2: "c6674d0", 3: "c6754c0"}.get(kind)
            if target:
                m.call(target, lv, flag)
            for i in range(0x180 // 4):
                m.w(lv + 0x2C + 4 * i, 0)
            m.w(lv + 0x1D8, 0)
            for i in range(0x4C // 4):
                m.w(lv + 0x1E0 + 4 * i, 0)
            if (flag == 0 or mut("automap-always")) and m.r(lv + 0x1BC):
                m.free(pool, m.r(lv + 0x1BC), 0xB7, FILE)
                m.w(lv + 0x1BC, 0)
            node = m.r(lv + 0x1CC)
            if node:
                while node:
                    nxt = m.r(node + 0xC)
                    m.free(pool, node, 0xC5, FILE)
                    node = nxt
                m.w(lv + 0x1CC, 0)
        rig.run("FUN_00642010", 0x642010, build, model, 2)

    def room_at(self):
        """FUN_00642c30."""
        rig, at = self.rig, self.at

        def build(img, rng):
            """A room with near rooms (rects small), an act with levels, a level maybe given."""
            for base in (0x700, 0x780, 0x800):
                put(img, base + 0x34, rng.randrange(0, 4))
                put(img, base + 0x38, rng.randrange(0, 4))
                put(img, base + 0x3C, rng.randrange(1, 3))
                put(img, base + 0x40, rng.randrange(1, 3))
            near = rng.randrange(0, 3)
            put(img, 0x700 + 0x2C, near)
            put(img, 0x700 + 8, at(0xA00))
            for i in range(near):
                put(img, 0xA00 + 4 * i, rng.choice((at(0x700), at(0x780), at(0x800))))
            self.state["lid"] = rng.choice((5, 6, 7))
            put(img, 0x47C, at(0x1200))
            put(img, 0x1200 + 0x1AC, at(0x1400))
            put(img, 0x1400 + 0x1AC, 0)
            put(img, 0x1200 + 0x1D0, 5)
            put(img, 0x1400 + 0x1D0, 6)
            put(img, 0x1200 + 0x10, rng.choice((0, at(0x700))))
            put(img, 0x1400 + 0x10, rng.choice((0, at(0x700))))
            put(img, 0xF00 + 0x10, rng.choice((0, at(0x700))))
            given = rng.choice((0, 0, at(0x1200)))
            put(img, 0x1200 + 0x10, 0) if given and rng.random() < 0.5 else None
            return {"ecx": rng.randrange(0, 5), "edx": rng.randrange(0, 5),
                    "stack": [at(0), rng.choice((0, at(0x700))), given]}

        def inside(m, rect, x, y):
            """FUN_0066b980 on the model: half-open, signed."""
            rx, ry, w, h = (s32(m.r(rect + o)) for o in (0, 4, 8, 12))
            return int(rx <= s32(x) < rx + w and ry <= s32(y) < ry + h)

        def model(m, a):
            """The room itself, then its near rooms, then the level's FUN_00642630."""
            x, y = a["ecx"], a["edx"]
            drlg, room, level = a["stack"]
            if room:
                if inside(m, room + 0x34, x, y):
                    return room
                for i in range(m.r(room + 0x2C)):
                    near = m.r(m.r(room + 8) + 4 * i)
                    if near != room and inside(m, near + 0x34, x, y):
                        return near
            if level == 0:
                lid = m.call("c6427a0", drlg, x, y)
                node = m.r(drlg + 0x47C)
                while node and m.r(node + 0x1D0) != lid:
                    node = m.r(node + 0x1AC)
                level = node or m.call("c642ae0", drlg, lid)
            if m.r(level + 0x10) == 0:
                m.call("c6424a0", level)
            return m.call("c642630", level, x, y if not mut("room-xy") else x)
        rig.run("FUN_00642c30", 0x642C30, build, model, 3)


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 120
    s = Suite(rig)
    s.teardown()
    s.room_at()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("room-xy", "flags-bit", "automap-always")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "Drlg.cpp level teardown and room search", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
