# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify small Outdoors.cpp, DrlgLogic and distance helpers of DRLG in stock game.exe 1.14d.

    uv run python drlg_outdoor.py [--mutation-check]

Runs game.exe's own code for FUN_00673fe0, FUN_00673ff0, FUN_00674160,
FUN_006741a0, FUN_006741e0, FUN_00674200, FUN_00675320, FUN_006755c0,
FUN_00678ad0, FUN_00678b10, FUN_00678b40, FUN_00678cb0, FUN_00678cd0,
FUN_00678d10 and FUN_00678d30 against hand-written models on random records
(_dungeon_rig.py): return value, callee log, arena, allocator blocks and the
bytes `ret n` pops. Tables in game.exe's data are read from the emulator.
Hooked, logged, not claimed: FUN_0066c040, FUN_0067c4f0, FUN_0067c570,
FUN_00678b80, FUN_00678c10, the five FUN_00678ad0 targets and the allocator.
`--mutation-check` breaks the model three ways.
"""
import argparse
import struct
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, s32

MUT = set()
F_OUT = 0x6F_05F8


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def put(img, off, value):
    """Write a dword at arena offset `off`."""
    img[off:off + 4] = (value & M32).to_bytes(4, "little")


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig):
        """Hook the callees this oracle leaves unclaimed."""
        self.rig = rig
        self.base = rig.arena
        self.idx = 0
        rig.callee("c66c040", 0x66_C040, 0, ("ecx", "edx"))
        rig.callee("c67c4f0", 0x67_C4F0, 3, ("ecx", "edx"))
        rig.callee("c67c570", 0x67_C570, 1, ("ecx", "edx"))
        rig.callee("c678b80", 0x67_8B80, 2, ("eax", "ecx"))
        rig.callee("c678c10", 0x67_8C10, 2, ())
        self.targets = (0x67_7750, 0x67_7790, 0x67_89B0, 0x67_8A20, 0x67_8A70)
        for i, a in enumerate(self.targets):
            rig.callee(f"t{i}", a, 0, ("eax",))
        rig.custom["c66c040"] = lambda e: self.base + 0x900
        rig.custom["c67c570"] = lambda e: self.flags
        rig.custom["c678b80"] = lambda e: self.idx
        rig.custom["c678c10"] = lambda e: self.idx
        self.flags = 0

        def dirty(e):
            """Allocator stand-in returning dirty memory, so the memset is visible."""
            from unicorn.x86_const import UC_X86_REG_EDX
            size = max(e.mu.reg_read(UC_X86_REG_EDX), 1)
            p = e.alloc(size)
            e.mu.mem_write(p, b"\xAA" * size)
            return p
        rig.custom["alloc"] = dirty

    def at(self, off):
        """Arena address of offset `off`."""
        return self.base + off

    def word(self, addr):
        """A dword of game.exe's image."""
        return struct.unpack("<I", self.rig.m.read(addr, 4))[0]

    def byte(self, addr):
        """A byte of game.exe's image."""
        return self.rig.m.read(addr, 1)[0]

    def levels(self):
        """FUN_00673fe0, 00673ff0, 00674160, 006741a0, 006741e0, 00674200, 00675320, 006755c0."""
        rig, at = self.rig, self.at

        def m_clear(m, a):
            """With EDX zero: +0x14 = 0 (otherwise unchanged)."""
            if a["edx"] == 0:
                m.w(a["ecx"] + 0x14, 0)
        rig.run("FUN_00673fe0", 0x673FE0, lambda img, rng: {"ecx": at(0), "edx": rng.choice((0, 1, 5))}, m_clear, 0)

        def b_vis(img, rng):
            """Two levels; the first one's Vis array (the hook's answer, at 0x900) may hold the second's id (`want`)."""
            put(img, 0x1D0, rng.randrange(1, 40))
            want = rng.randrange(1, 40)
            put(img, 0x200 + 0x1D0, want)
            for i in range(8):
                put(img, 0x900 + 4 * i, rng.choice((0, 0, 7)))
            if rng.random() < 0.6:
                put(img, 0x900 + 4 * rng.randrange(8), 0)
                put(img, 0x900 + 4 * rng.randrange(8), 0)
            hit = rng.randrange(8)
            if rng.random() < 0.7:
                put(img, 0x900 + 4 * hit, want)
            put(img, 0x1B4, at(0x600))
            return {"ecx": at(0), "edx": at(0x200)}

        def m_vis(m, a):
            """FUN_0066c040(act, id) -> Vis[8]; the bit 1 << (i + 4) for the index holding the other level's id, else 0."""
            arr = m.call("c66c040", m.r(a["ecx"] + 0x1B4), m.r(a["ecx"] + 0x1D0))
            want = m.r(a["edx"] + 0x1D0)
            for i in range(8):
                if m.r(arr + 4 * i) == want:
                    return 1 << (i + 4 + (1 if mut("vis-bit") else 0))
            return 0
        rig.run("FUN_00673ff0", 0x673FF0, b_vis, m_vis, 0)

        def b_lv(img, rng):
            """A level whose +0x14 record holds two grids at +4 and +0x2c; a cell (x in EDX, y on the stack)."""
            put(img, 0x14, at(0x300))
            return {"ecx": at(0), "edx": rng.getrandbits(32), "stack": [rng.getrandbits(32)]}

        def m_two_ops(second):
            """Two FUN_0067c4f0 calls on the +4 and +0x2c grids with the given second value."""
            def model(m, a):
                """(grid + 4, x, y, 0, 3) then (grid + 0x2c, x, y, second, 3)."""
                rec = m.r(a["ecx"] + 0x14)
                m.call("c67c4f0", rec + 4, a["edx"], a["stack"][0], 0, 3)
                m.call("c67c4f0", rec + 0x2C, a["edx"], a["stack"][0], second, 3)
            return model
        rig.run("FUN_00674160", 0x674160, b_lv, m_two_ops(0), 1)
        rig.run("FUN_006741a0", 0x6741A0, b_lv, m_two_ops(0x100), 1)

        def b_flag(img, rng):
            """The +0x14 record and the hooked grid-flags answer."""
            put(img, 0x14, at(0x300))
            self.flags = rng.choice((0, 0x400, 0x1B81, 0x1, 0x800, rng.getrandbits(32)))
            return {"ecx": at(0), "stack": [rng.getrandbits(32)]}

        def m_bit10(m, a):
            """FUN_0067c570(grid + 0x2c, y) bit 10 clear."""
            f = m.call("c67c570", m.r(a["ecx"] + 0x14) + 0x2C, 0, a["stack"][0])
            return (~(f >> 10)) & 1
        rig.run("FUN_006741e0", 0x6741E0, b_flag, m_bit10, 1)

        def m_mask(m, a):
            """FUN_0067c570(grid + 0x2c, y) has none of the bits 0x1b81 set."""
            f = m.call("c67c570", m.r(a["ecx"] + 0x14) + 0x2C, 0, a["stack"][0])
            return int(f & (0x1B81 if not mut("mask") else 0x1B80) == 0)
        rig.run("FUN_00674200", 0x674200, b_flag, m_mask, 1)

        def b_alloc(img, rng):
            """A level whose act (+0x1b4) holds the pool at +0x478."""
            put(img, 0x1B4, at(0x600))
            return {"ecx": at(0)}

        def m_alloc(m, a):
            """Allocate 0x268 (Outdoors.cpp line 0x34c), zero it, store at +0x14."""
            r = a["ecx"]
            m.w(r + 0x14, m.alloc(m.r(m.r(r + 0x1B4) + 0x478), 0x268, 0x34C, F_OUT))
        rig.run("FUN_00675320", 0x675320, b_alloc, m_alloc, 0)

        def b_tab(img, rng):
            """Indices into the two data tables."""
            return {"ecx": rng.randrange(0, 3), "edx": rng.randrange(0, 4), "stack": [rng.randrange(0, 9)]}

        def m_tab(m, a):
            """v = T0[ecx + 3 * edx]; s >= 4: T2[s + 2v - 4], else T1[s + 4v]."""
            v = s32(self.word(0x6F_0FD0 + 4 * (a["ecx"] + 3 * a["edx"])))
            s = a["stack"][0]
            if s >= 4:
                return self.word(0x6F_06F0 + 4 * (s + 2 * v - 4))
            return self.word(0x6F_0630 + 4 * (s + 4 * v))
        rig.run("FUN_006755c0", 0x6755C0, b_tab, m_tab, 1)

    def distance(self):
        """FUN_00678ad0, 00678b10, 00678b40, 00678cb0, 00678cd0, 00678d10, 00678d30."""
        rig, at = self.rig, self.at

        def b_kind(img, rng):
            """A kind byte in DL and a pointer in ECX."""
            return {"ecx": at(0), "edx": rng.randrange(0, 7) | (rng.getrandbits(24) << 8)}

        def m_kind(m, a):
            """Kinds 0..4 tail-jump to five targets with the pointer in EAX; others return."""
            k = a["edx"] & 0xFF
            if k <= 4:
                return m.call(f"t{k}", a["ecx"])
            return None
        rig.run("FUN_00678ad0", 0x678AD0, b_kind, m_kind, 0)

        def b_xy(img, rng):
            """(x1 ECX, y1 EDX, x2, y2) small, signed."""
            return {"ecx": rng.randrange(-6, 6) & M32, "edx": rng.randrange(-6, 6) & M32,
                    "stack": [rng.randrange(-6, 6) & M32, rng.randrange(-6, 6) & M32]}

        def m_oct(m, a):
            """min(|dx|, |dy|) + 2 * max(|dx|, |dy|)."""
            dx, dy = abs(s32(a["ecx"]) - s32(a["stack"][0])), abs(s32(a["edx"]) - s32(a["stack"][1]))
            return (min(dx, dy) + (1 if mut("oct") else 0) + 2 * max(dx, dy)) & M32
        rig.run("FUN_00678b10", 0x678B10, b_xy, m_oct, 2)

        def b_packed(img, rng):
            """Two points packed as x | y << 16."""
            def pk():
                """A packed point."""
                return (rng.randrange(0, 20) | (rng.randrange(0, 20) << 16)) & M32
            return {"stack": [pk(), pk()]}

        def m_packed(m, a):
            """The same distance on the unsigned 16-bit halves."""
            p, q = a["stack"]
            dx, dy = abs((q & 0xFFFF) - (p & 0xFFFF)), abs((q >> 16) - (p >> 16))
            return (min(dx, dy) + 2 * max(dx, dy)) & M32
        rig.run("FUN_00678b40", 0x678B40, b_packed, m_packed, 2)

        def b_tab(img, rng):
            """The hooked index answer and the register arguments."""
            self.idx = rng.randrange(0, 12)
            return {"ecx": rng.getrandbits(32), "edx": rng.getrandbits(32), "stack": [rng.getrandbits(32), rng.getrandbits(32)]}

        def m_b(m, a):
            """FUN_00678b80(eax = a, ecx = b; stack ecx, edx) -> index; the byte at 0x6f1648 + 3 * index."""
            idx = m.call("c678b80", a["stack"][0], a["stack"][1], a["ecx"], a["edx"])
            return self.byte(0x6F_1648 + 3 * idx)
        rig.run("FUN_00678cb0", 0x678CB0, b_tab, m_b, 2)

        def m_c(m, a):
            """FUN_00678c10(a, b) -> index; the byte at 0x6f1648 + 3 * index."""
            idx = m.call("c678c10", a["stack"][0], a["stack"][1])
            return self.byte(0x6F_1648 + 3 * idx)
        rig.run("FUN_00678cd0", 0x678CD0, b_tab, m_c, 2)

        def m_d(m, a):
            """FUN_00678c10(a, b) -> index; the dword at 0x6f1518 + 12 * index."""
            idx = m.call("c678c10", a["stack"][0], a["stack"][1])
            return self.word(0x6F_1518 + 12 * idx)
        rig.run("FUN_00678d10", 0x678D10, b_tab, m_d, 2)

        def b_out(img, rng):
            """As above plus a three-dword output record."""
            args = b_tab(img, rng)
            args["stack"].append(at(0x300))
            return args

        def m_e(m, a):
            """FUN_00678b80 -> index; out = the three dwords at 0x6f1518 + 12 * index."""
            idx = m.call("c678b80", a["stack"][0], a["stack"][1], a["ecx"], a["edx"])
            for k in range(3):
                m.w(a["stack"][2] + 4 * k, self.word(0x6F_1518 + 12 * idx + 4 * k))
        rig.run("FUN_00678d30", 0x678D30, b_out, m_e, 3)


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 80
    s = Suite(rig)
    s.levels()
    s.distance()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("vis-bit", "mask", "oct")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "Outdoors.cpp and distance helper", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
