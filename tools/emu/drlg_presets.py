# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify three Preset.cpp object-preset builders in stock game.exe 1.14d.

    uv run python drlg_presets.py [--mutation-check]

Runs game.exe's own code for FUN_00666220, FUN_006663c0 (which runs
FUN_00666220 for real) and FUN_00666990 against hand-written models on random
records (_dungeon_rig.py): callee log, arena, allocator blocks and the bytes
`ret n` pops. Hooked, logged, not claimed: FUN_00643560 (room-to-world
transform, replaced by a fixed linear map the model shares), FUN_0066bf30
(preset unit allocator, returns arena slots), FUN_0067c570 (grid flags),
FUN_0067cd30, FUN_0067cdc0, FUN_006661a0 and the allocator. The grid object
FUN_006663c0 builds lives on the emulated stack, so its address is compared
only for being the same in every call. `--mutation-check` breaks the model
three ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt, require, s32

MUT = set()
F_PRESET = 0x6E_E7C4
ADD_SET = (0, 4, 8, 0x10, 0x1D, 0x27)


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def put(img, off, value):
    """Write a dword at arena offset `off`."""
    img[off:off + 4] = (value & M32).to_bytes(4, "little")


def world(x, y):
    """The stand-in for FUN_00643560: a fixed linear map from room to world coordinates."""
    return (x * 3 + y) & M32, (y * 5 + x + 1) & M32


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig):
        """Hook the callees this oracle leaves unclaimed."""
        from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX
        self.rig = rig
        self.base = rig.arena
        self.flagfn = lambda x, y: 0
        self.units = []
        self.grids = []
        self.fills = []
        self.exp_fills = []
        rig.callee("c643560", 0x64_3560, 2, (), nlog=0)
        rig.callee("c66bf30", 0x66_BF30, 5, ("ecx", "edx"))
        rig.callee("c67c570", 0x67_C570, 1, ("edx",))
        rig.callee("c67cd30", 0x67_CD30, 3, ("edx",), nlog=0)
        rig.callee("c67cdc0", 0x67_CDC0, 0, ())
        rig.callee("c6661a0", 0x66_61A0, 0, ("ecx", "eax", "edi", "esi"))

        def transform(e):
            """FUN_00643560 stand-in: rewrite the (x, y) the two pointers hold."""
            px, py = e.arg(0), e.arg(1)
            x, y = world(e.r32(px), e.r32(py))
            e.mu.mem_write(px, x.to_bytes(4, "little"))
            e.mu.mem_write(py, y.to_bytes(4, "little"))
            return 0

        def new_unit(e):
            """FUN_0066bf30 stand-in: hand out the next preset unit slot of the arena."""
            slot = self.base + 0x1000 + 0x40 * len(self.units)
            self.units.append(slot)
            return slot

        def flags(e):
            """FUN_0067c570 stand-in: note the grid pointer, answer the case's flags for the cell."""
            self.grids.append(e.mu.reg_read(UC_X86_REG_ECX))
            return self.flagfn(e.mu.reg_read(UC_X86_REG_EDX), e.arg(0))

        def fill(e):
            """FUN_0067cd30 stand-in: note the grid pointer, the rectangle and the width."""
            self.grids.append(e.mu.reg_read(UC_X86_REG_ECX))
            self.fills.append(tuple(e.r32(e.arg(0) + 4 * k) for k in range(4)) + (e.arg(1),))
            return 0
        rig.custom["c643560"] = transform
        rig.custom["c66bf30"] = new_unit
        rig.custom["c67c570"] = flags
        rig.custom["c67cd30"] = fill
        rig.custom["c67cdc0"] = lambda e: self.grids.append(e.mu.reg_read(UC_X86_REG_ECX)) or 0

    def at(self, off):
        """Arena address of offset `off`."""
        return self.base + off

    def reset(self):
        """Clear the per-case notes."""
        self.units.clear()
        self.grids.clear()
        self.fills.clear()
        self.exp_fills.clear()

    def strip(self, m, mp, pool, xoff, grid_calls):
        """The body of FUN_00666220 on the model: five preset units per row, skipping rows past some floor types."""
        y = 0
        size_y = s32(m.r(mp + 0x1C))
        while y < size_y:
            m.call("c643560")
            cx, cy = world((m.r(mp + 0x10) + xoff) & M32, (m.r(mp + 0x14) + y) & M32)
            wx = (cx - 5) & M32
            for k, (uid, dx) in enumerate(((0x28, 0), (0x29, 5), (0x29, 10), (0x29, 15), (0x2A, 20))):
                if mut("unit-id") and k == 4:
                    uid = 0x29
                unit = m.call("c66bf30", 0, pool, 2, uid, 0, (wx + dx) & M32, cy)
                m.w(unit + 0x1C, m.r(unit + 0x1C) | 1)
                m.w(unit + 0xC, m.r(mp + 0x38))
                m.w(mp + 0x38, unit)
            gx = 0 if s32(xoff) <= 0 else xoff
            fl = m.call("c67c570", gx, y)
            if (fl >> 20) & 0x3F == 4 and (fl >> 8) & 0xFF in ADD_SET:
                y += 3
            y += 1

    def make_map(self, img, rng):
        """A map record at 0 with an origin, a size, a unit list head and a floor layer chain."""
        put(img, 0x10, rng.randrange(0, 40))
        put(img, 0x14, rng.randrange(0, 40))
        put(img, 0x18, rng.randrange(1, 5))
        put(img, 0x1C, rng.choice((0, 1, 2, 5)))
        put(img, 0x38, rng.choice((0, 0x1234)))

    def flags_picker(self, rng):
        """A cell flag source: mostly boring, sometimes a type 2 / sub 0x18 or type 4 / listed-sub cell."""
        def pick(x, y):
            """Cell flags."""
            r = rng.random()
            if r < 0.3:
                return (2 << 20) | (0x18 << 8)
            if r < 0.6:
                return (4 << 20) | (rng.choice(ADD_SET + (1, 2, 3, 0x28)) << 8)
            return rng.getrandbits(32)
        return pick

    def strips(self):
        """FUN_00666220: the five-unit rows of a preset map."""
        rig, at = self.rig, self.at

        def build(img, rng):
            """A map and an x offset (-1 for the global case)."""
            self.reset()
            self.make_map(img, rng)
            self.flagfn = self.flags_picker(rng)
            return {"regs": {"esi": at(0), "edi": rng.getrandbits(32)}, "stack": [rng.choice((M32, 0, 1, 7, 12)), 0x5000]}

        def model(m, a):
            """Per row: five units linked at the map's list head, then maybe skip three rows."""
            self.strip(m, a["regs"]["esi"], a["regs"]["edi"], a["stack"][0], None)
            require(len(set(self.grids)) <= 1, "FUN_0067c570 got different grid pointers")
        rig.run("FUN_00666220", 0x666220, build, model, 2, ret=False)

    def build_presets(self):
        """FUN_006663c0: scan the floor layer for type 2 / sub 0x18 cells and place the object presets."""
        rig, at = self.rig, self.at

        def build(img, rng):
            """A map (def 0x1b now and then) with floor layer pointers and a pool."""
            self.reset()
            self.make_map(img, rng)
            put(img, 0xC, at(0x100))
            put(img, 0x100 + 0x3C, rng.getrandbits(32))
            put(img, 8, at(0x200))
            put(img, 0x200, 0x1B if rng.random() < 0.25 else rng.choice((0, 5, 0x1A)))
            self.flagfn = self.flags_picker(rng)
            return {"ecx": at(0), "regs": {"eax": rng.getrandbits(32), "edi": 0, "esi": 0}}

        def model(m, a):
            """Fill the grid; def 0x1b: one global placement; else for each matching cell place and skip four."""
            mp, pool = a["ecx"], a["regs"]["eax"]
            sx, sy = m.r(mp + 0x18), m.r(mp + 0x1C)
            self.exp_fills.append((0, 0, sx, sy, (sx + 1) & M32))
            m.call("c67cd30", m.r(m.r(mp + 0xC) + 0x3C))
            rx, ry = m.r(mp + 0x10), m.r(mp + 0x14)
            if m.r(m.r(mp + 8)) == 0x1B:
                m.call("c6661a0", rx, ry, pool, mp)
                self.strip(m, mp, pool, M32, None)
            else:
                x = 0
                while x < s32(sx):
                    fl = m.call("c67c570", x, 0)
                    if (fl >> 20) & 0x3F == 2 and (fl >> 8) & 0xFF == (0x18 if not mut("sub") else 0x19):
                        m.call("c6661a0", (x + rx + 1) & M32, ry, pool, mp)
                        self.strip(m, mp, pool, x, None)
                        x += 4
                    x += 1
            m.call("c67cdc0")
            require(self.fills == self.exp_fills, f"fill {self.fills}, model {self.exp_fills}")
            require(len(set(self.grids)) == 1, "the grid pointer changed during the run")
        rig.run("FUN_006663c0", 0x6663C0, build, model, 0, ret=False)

    def nav(self):
        """FUN_00666990: collect up to six burial-ground navigation points from a preset room's grid."""
        rig, at = self.rig, self.at

        def build(img, rng):
            """A preset room at 0x100 with a size, a coordinate origin and its preset data at 0x300."""
            self.reset()
            put(img, 0x100 + 0x20, at(0x300))
            put(img, 0x100 + 0x34, rng.randrange(0, 30))
            put(img, 0x100 + 0x38, rng.randrange(0, 30))
            put(img, 0x100 + 0x3C, rng.choice((0, 2, 4, 5)))
            put(img, 0x100 + 0x40, rng.choice((0, 1, 3, 4)))
            put(img, 0x300 + 0xF0, rng.choice((0, 0, 0, 0x55)))
            put(img, 0x300 + 0xF4, rng.getrandbits(32))

            def pick(x, y):
                """Burial-ground marker cells (type 10, sub 0x17-0x1b) often, others otherwise."""
                r = rng.random()
                if r < 0.5:
                    return (10 << 20) | (rng.randrange(0x15, 0x1E) << 8)
                return rng.getrandbits(32)
            self.flagfn = pick
            return {"ecx": rng.getrandbits(32), "regs": {"ebx": at(0x100)}}

        def model(m, a):
            """Allocate 0x30 bytes (line 0x7ac) once; each marker cell becomes a (x + 2, y + 2) world point; store the count."""
            room = a["regs"]["ebx"]
            pre = m.r(room + 0x20)
            if m.r(pre + 0xF0):
                return
            blk = m.alloc(a["ecx"], 0x30, 0x7AC, F_PRESET)
            m.w(pre + 0xF0, blk)
            n = 0
            for y in range(max(s32(m.r(room + 0x40)), 0)):
                for x in range(max(s32(m.r(room + 0x3C)), 0)):
                    fl = m.call("c67c570", x, y)
                    if (fl >> 20) & 0x3F == 10 and 0 <= ((fl >> 8) & 0xFF) - 0x17 <= 4 and n < (6 if not mut("limit") else 7):
                        m.call("c643560")
                        px, py = world((m.r(room + 0x34) + x) & M32, (m.r(room + 0x38) + y) & M32)
                        m.w(blk + 8 * n, (px + 2) & M32)
                        m.w(blk + 8 * n + 4, (py + 2) & M32)
                        n += 1
            m.w(pre + 0xF4, n)
            require(set(self.grids) <= {pre + 0x10}, f"grid pointers {self.grids}")
        rig.run("FUN_00666990", 0x666990, build, model, 0, ret=False)


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 200
    s = Suite(rig)
    s.strips()
    s.build_presets()
    s.nav()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("unit-id", "sub", "limit")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "Preset.cpp object preset", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
