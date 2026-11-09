# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the RoomTile.cpp tile counters and the LvlSub tile-type match in stock game.exe 1.14d.

    uv run python drlg_tiles.py [--mutation-check]

Runs game.exe's own code for FUN_0066d290, FUN_0066ece0 (CountTilesFromGrid),
FUN_0066edb0, FUN_0066eee0, FUN_0066f0b0 and FUN_0066fe00 against hand-written models on
random records (_dungeon_rig.py): return value, callee log, arena, allocator
blocks and the bytes `ret n` pops. The CRT memset runs for real, with an
allocator that returns dirty memory. Hooked, logged, not claimed:
FUN_0067c570 (grid flags), FUN_0067c550 (grid cell pointer), FUN_0066b980,
FUN_00604ae0, FUN_00604bc0, FUN_0066f050 and the allocator. `--mutation-check` breaks
the model six ways.
"""
import argparse
import struct
import sys
import zlib

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, s32

MUT = set()
F_ROOMTILE = 0x6E_F540


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
        from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX
        self.rig = rig
        self.base = rig.arena
        self.flagfn = lambda g, x, y: 0
        self.count = 0
        self.tile = 0
        rig.callee("c67c570", 0x67_C570, 1, ("ecx", "edx"))
        rig.callee("c67c550", 0x67_C550, 1, ("ecx", "edx"))
        rig.callee("c66b980", 0x66_B980, 1, ("ecx", "edx"))
        rig.callee("c604ae0", 0x60_4AE0, 6, (), nlog=4)
        rig.callee("c604bc0", 0x60_4BC0, 1, ())
        rd = lambda e, r: e.mu.reg_read(r)
        rig.custom["c67c570"] = lambda e: self.flagfn(rd(e, UC_X86_REG_ECX), rd(e, UC_X86_REG_EDX), e.arg(0))
        rig.custom["c67c550"] = lambda e: self.base + 0x900 + 4 * ((rd(e, UC_X86_REG_EDX) + 8 * e.arg(0)) % 64)
        rig.custom["c66b980"] = lambda e: rig.rng.getrandbits(1)

        def lookup(e):
            """FUN_00604ae0 stand-in: write one tile at the buffer, answer the count."""
            if e.arg(5) != 0x28:
                return 0
            e.mu.mem_write(e.arg(4), self.tile.to_bytes(4, "little"))
            return self.count
        rig.custom["c604ae0"] = lookup
        rig.custom["c604bc0"] = lambda e: rig.rng.choice((0, 0x100, 0x100, 0x1F, 0x1FF))

        def dirty(e):
            """Allocator stand-in returning dirty memory, so the memset is visible."""
            from unicorn.x86_const import UC_X86_REG_EDX as edx
            size = max(e.mu.reg_read(edx), 1)
            p = e.alloc(size)
            e.mu.mem_write(p, b"\xAA" * size)
            return p
        rig.custom["alloc"] = dirty

    def at(self, off):
        """Arena address of offset `off`."""
        return self.base + off

    def room(self, img, rng):
        """A room at 0x100 with random coordinates and size, a tile grid at 0x200 with random counters; returns the room address."""
        put(img, 0x100 + 0x34, rng.randrange(0, 30))
        put(img, 0x100 + 0x38, rng.randrange(0, 30))
        put(img, 0x100 + 0x3C, rng.randrange(0, 4))
        put(img, 0x100 + 0x40, rng.randrange(0, 4))
        put(img, 0x100 + 0x54, self.at(0x200))
        for k in range(0x14, 0x30, 4):
            put(img, 0x200 + k, rng.randrange(0, 100))
        return self.at(0x100)

    def count_floors(self):
        """FUN_0066ece0 (CountTilesFromGrid), FUN_0066edb0 and FUN_0066d290: tile counters from a grid."""
        rig, at = self.rig, self.at

        def b_count(img, rng):
            """A room, a grid, a floor flag and the two kill-edge flags."""
            room = self.room(img, rng)
            for i in range(64):
                put(img, 0x900 + 4 * i, rng.choice((0, 1, 2, 3, 0x8000000, 0x8000001, 0x8000002, 0x7, rng.getrandbits(32))))
            return {"ecx": room, "edx": at(0x700), "stack": [rng.choice((0, 1)), rng.choice((0, 1, 2)), rng.choice((0, 1, 2))]}

        def m_count(m, a):
            """Per cell: wall bit -> wall count; floor bit (or inside the room when asked) -> floor count; roof bit -> roof count."""
            room, grid = a["ecx"], a["edx"]
            floors, kx, ky = a["stack"]
            tg = m.r(room + 0x54)
            w = (1 if kx == 0 else 0) + m.r(room + 0x3C)
            h = (1 if ky == 0 else 0) + m.r(room + 0x40)
            for y in range(max(h, 0)):
                for x in range(max(w, 0)):
                    cell = m.call("c67c550", grid, x, y)
                    flags = m.r(cell)
                    if flags & 1:
                        m.w(tg + 0x18, m.r(tg + 0x18) + 1)
                    inside = False
                    if not flags & 2 and floors:
                        inside = m.call("c66b980", room + 0x34, x + m.r(room + 0x34), y + m.r(room + 0x38))
                    if flags & 2 or inside:
                        m.w(tg + 0x20, m.r(tg + 0x20) + 1)
                    if flags & 0x8000000:
                        m.w(tg + 0x28, m.r(tg + 0x28) + (1 if not mut("roof") else 2))
        rig.run("FUN_0066ece0", 0x66ECE0, b_count, m_count, 3, ret=False)

        def b_wall(img, rng):
            """Two grids whose cell flags come from the hook: orientation and floor."""
            room = self.room(img, rng)
            self.flagfn = lambda g, x, y: (rng.choice((3, 10, 11, 12, 0, 5, 1)) if g == at(0x740)
                                           else rng.choice((0, 1, 0x80000000, 0xFFFFFFFF, 5)))
            return {"ecx": room, "edx": at(0x700), "stack": [at(0x740), rng.choice((0, 1)), rng.choice((0, 1))]}

        def m_wall(m, a):
            """Orientation 3 -> wall; 10 or 11 -> wall when the floor grid's flags are not negative, else 6 floors."""
            room = a["ecx"]
            fgrid, ogrid = a["edx"], a["stack"][0]
            tg = m.r(room + 0x54)
            w = (1 if a["stack"][1] == 0 else 0) + m.r(room + 0x3C)
            h = (1 if a["stack"][2] == 0 else 0) + m.r(room + 0x40)
            for y in range(max(h, 0)):
                for x in range(max(w, 0)):
                    o = m.call("c67c570", ogrid, x, y)
                    if o == 3:
                        m.w(tg + 0x18, m.r(tg + 0x18) + 1)
                    elif (o - 10) & M32 < 2:
                        fl = m.call("c67c570", fgrid, x, y)
                        if s32(fl) >= 0:
                            m.w(tg + 0x18, m.r(tg + 0x18) + 1)
                        else:
                            m.w(tg + 0x20, m.r(tg + 0x20) + (6 if not mut("six") else 5))
        rig.run("FUN_0066edb0", 0x66EDB0, b_wall, m_wall, 3, ret=False)

        def b_anim(img, rng):
            """A room with projects at +0x68, a grid, an orientation grid (or a constant)."""
            room = self.room(img, rng)
            self.count = rng.choice((0, 1, 2, 4))
            self.tile = rng.getrandbits(32)
            self.flagfn = lambda g, x, y: (rng.choice((0, 1, 2, 3, 0x8000000, 0x8000001, 0x4, 0x1234502, 0x3F00301, 0x8000002 | rng.getrandbits(32) & 0x3FFFF00))
                                           if g == at(0x700) else rng.randrange(0, 5))
            orient = at(0x740) if rng.random() < 0.6 else 0
            return {"ecx": room, "edx": at(0x700), "stack": [orient, rng.getrandbits(8), rng.choice((0, 1)), rng.choice((0, 1))]}

        def m_anim(m, a):
            """Per cell with flag bits 0x8000003: look the tile up; an animated one (0x100) adds count-1 to its counter and flags the room."""
            room, grid = a["ecx"], a["edx"]
            orient, fallback, kx, ky = a["stack"]
            tg = m.r(room + 0x54)
            w = (1 if kx == 0 else 0) + m.r(room + 0x3C)
            h = (1 if ky == 0 else 0) + m.r(room + 0x40)
            for y in range(max(h, 0)):
                for x in range(max(w, 0)):
                    fl = m.call("c67c570", grid, x, y)
                    if not fl & 0x8000003:
                        continue
                    o = m.call("c67c570", orient, x, y) if orient else fallback
                    main, sub = ((fl >> 20) & 0x3F, (fl >> 8) & 0xFF) if fl else (0, 0)
                    n = m.call("c604ae0", room + 0x68, o, main, sub)
                    if n == 0:
                        continue
                    if m.call("c604bc0", self.tile) & 0x100:
                        field = 0x20 if fl & 2 else 0x18 if fl & 1 else 0x28
                        m.w(tg + field, m.r(tg + field) + n - 1)
                        m.w(room + 0x28, m.r(room + 0x28) | (0x8000000 if not mut("anim-flag") else 0x4000000))
        rig.run("FUN_0066d290", 0x66D290, b_anim, m_anim, 4, ret=False)

    def alloc_arrays(self):
        """FUN_0066eee0: allocate the floor, wall and roof tile arrays by their counts."""
        rig, at = self.rig, self.at

        def build(img, rng):
            """A room, its level, act pool and a tile grid with random counts and a roof pointer that may be set."""
            put(img, 0x100 + 0x58, at(0x300))
            put(img, 0x300 + 0x1B4, at(0x400))
            put(img, 0x400 + 0x478, rng.getrandbits(32))
            put(img, 0x100 + 0x54, at(0x200))
            for k in (0x20, 0x18, 0x28):
                put(img, 0x200 + k, rng.choice((0, 1, 3, 7)))
            put(img, 0x200 + 0x24, rng.choice((0, 0x1234)))
            return {"ecx": at(0x100)}

        def model(m, a):
            """Floor (line 0x545), wall (0x54b) and roof (0x551, only when no roof array yet) blocks of count * 0x30, zeroed."""
            room = a["ecx"]
            tg = m.r(room + 0x54)
            pool = m.r(m.r(m.r(room + 0x58) + 0x1B4) + 0x478)
            for count_off, ptr_off, line in ((0x20, 0x1C, 0x545), (0x18, 0x14, 0x54B), (0x28, 0x24, 0x551)):
                n = m.r(tg + count_off)
                if n == 0:
                    continue
                if ptr_off == 0x24 and m.r(tg + ptr_off) != 0 and not mut("roof-guard"):
                    continue
                m.w(tg + ptr_off, m.alloc(pool, n * 0x30, line, F_ROOMTILE))
        rig.run("FUN_0066eee0", 0x66EEE0, build, model, 0, ret=False)

    def free_tiles(self):
        """FUN_0066f0b0: free a room's tile arrays (FUN_0066f050), its map links and animated tile lists, and the tile grid."""
        rig, at = self.rig, self.at
        rig.callee("c66f050", 0x66_F050, 0, ("eax", "edi"))

        def build(img, rng):
            """A room with a level, act pool and a tile grid (or none) holding link and animation chains."""
            put(img, 0x100 + 0x58, at(0x300))
            put(img, 0x300 + 0x1B4, at(0x400))
            put(img, 0x400 + 0x478, rng.getrandbits(32))
            put(img, 0x100 + 0x54, 0 if rng.random() < 0.15 else at(0x200))
            links = rng.randrange(0, 4)
            put(img, 0x200, at(0x500) if links else 0)
            for k in range(links):
                put(img, 0x500 + 0x20 * k + 8, at(0x500 + 0x20 * (k + 1)) if k + 1 < links else 0)
            anims = rng.randrange(0, 4)
            put(img, 0x200 + 4, at(0x700) if anims else 0)
            for k in range(anims):
                put(img, 0x700 + 0x20 * k, rng.getrandbits(32))
                put(img, 0x700 + 0x20 * k + 0x10, at(0x700 + 0x20 * (k + 1)) if k + 1 < anims else 0)
            return {"ecx": at(0x100)}

        def model(m, a):
            """Free the arrays, then each map link (0x5ad), each animation grid's data (0x5b9) and itself (0x5ba), then the grid (0x5bd)."""
            room = a["ecx"]
            pool = m.r(m.r(m.r(room + 0x58) + 0x1B4) + 0x478)
            m.call("c66f050", room, pool)
            tg = m.r(room + 0x54)
            if tg == 0:
                return
            link = m.r(tg)
            while link:
                nxt = m.r(link + 8)
                m.free(pool, link, 0x5AD, F_ROOMTILE)
                link = nxt
            m.w(tg, 0)
            anim = m.r(tg + 4)
            while anim:
                nxt = m.r(anim + 0x10)
                m.free(pool, m.r(anim), 0x5B9, F_ROOMTILE)
                m.free(pool, anim, 0x5BA if not mut("anim-line") else 0x5B9, F_ROOMTILE)
                anim = nxt
            m.free(pool, tg, 0x5BD, F_ROOMTILE)
            m.w(room + 0x54, 0)
        rig.run("FUN_0066f0b0", 0x66F0B0, build, model, 0, ret=False)

    def match_types(self):
        """FUN_0066fe00: can a LvlSub group be placed here (tile types, floor and wall flags agree)."""
        rig, at = self.rig, self.at
        # arena: group 0x000, drlg grids 0x100 (+4/+0x14/+0x24 -> grid records 0x700/0x740/0x780),
        # sub record 0x200 (+0x50 file 0x500), file 0x500

        def build(img, rng):
            """A group rectangle, the sub record's file layer counts and the hook's value functions."""
            gx, gy = rng.randrange(0, 20), rng.randrange(0, 20)
            rx, ry = rng.randrange(0, 20), rng.randrange(0, 20)
            put(img, 0, gx)
            put(img, 4, gy)
            put(img, 8, rng.choice((-1, 0, 1, 2, 3)))
            put(img, 12, rng.choice((0, 1, 2, 3)))
            put(img, 0x100 + 4, at(0x700))
            put(img, 0x100 + 0x14, at(0x740))
            put(img, 0x100 + 0x24, at(0x780))
            put(img, 0x200 + 0x50, at(0x500))
            put(img, 0x500 + 0x14, rng.choice((0, 1, 2)))
            put(img, 0x500 + 0x18, rng.choice((0, 1, 2)))
            pdiff = rng.choice((0, 0, 0, 1, 3))
            pbit = rng.choice((0, 0, 2, 6))
            salt = rng.getrandbits(32)
            kinds = {at(0x200) + 0x54: "Ts", at(0x700): "Tr", at(0x200) + 0xF4: "Fs", at(0x780): "Fr",
                     at(0x200) + 0xA4: "Ws", at(0x740): "Wr"}

            def flags(g, x, y):
                """Cell flags of each grid: the sub and room values agree unless the case perturbs a room cell."""
                kind = kinds[g]
                cx, cy = (x - gx, y - gy) if kind[1] == "s" else (x - rx, y - ry)
                h = zlib.crc32(struct.pack("<4I", salt, cx & M32, cy & M32, 0))
                if kind[0] == "T":
                    base = h % 3
                    return base + (1 if kind[1] == "r" and (h >> 8) % 8 < pdiff else 0)
                base = [0, 1, 2, 3, 0x102, 0x1000001, 0x3F00302, 0x2, 0x1][(h >> 4) % 9]
                if kind[1] == "s":
                    return base
                if (h >> 16) % 8 < pbit:
                    base ^= [2, 1, 0x100, 0x4, 0x1000000, 0x10000][(h >> 20) % 6]
                return base
            self.flagfn = flags
            return {"regs": {"esi": at(0)}, "stack": [rx, ry, at(0x100), at(0x200)]}

        def model(m, a):
            """Walk the group's rectangle; the tile types, then the floor and wall flags must agree."""
            g = a["regs"]["esi"]
            rx, ry, drlg, sub = a["stack"]
            gx, gy, gw, gh = m.r(g), m.r(g + 4), s32(m.r(g + 8)), s32(m.r(g + 12))
            file = m.r(sub + 0x50)
            for row in range(max(gh, 0)):
                for col in range(max(gw, 0)):
                    y = ry + row
                    s_type = m.call("c67c570", sub + 0x54, gx + col, gy + row)
                    r_type = m.call("c67c570", m.r(drlg + 4), rx + col, y)
                    if s_type != r_type:
                        return 0
                    fl = m.call("c67c570", sub + 0xF4, gx + col, gy + row) if m.r(file + 0x18) else 0
                    if fl & 2:
                        rf = m.call("c67c570", m.r(drlg + 0x24), rx + col, y)
                        if not rf & 2:
                            return 0
                        if (rf ^ fl) & (0x3F0FF00 if not mut("mask") else 0x3F0FE00):
                            return 0
                    wl = m.call("c67c570", sub + 0xA4, gx + col, gy + row) if m.r(file + 0x14) else 0
                    if wl & 1:
                        rw = m.call("c67c570", m.r(drlg + 0x14), rx + col, y)
                        if not rw & 1:
                            return 0
                        if (rw ^ wl) & 0x3F0FF00:
                            return 0
            return 1
        rig.run("FUN_0066fe00", 0x66FE00, build, model, 4)


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 200
    s = Suite(rig)
    s.count_floors()
    s.alloc_arrays()
    s.free_tiles()
    s.match_types()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("roof", "six", "anim-flag", "mask", "roof-guard", "anim-line")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "RoomTile.cpp counter and LvlSub match", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
