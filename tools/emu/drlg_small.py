# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 19 small DRLG helpers (Drlg.cpp, DrlgRoom.cpp, RoomTile.cpp, Preset.cpp, DrlgGrid.cpp) in stock game.exe 1.14d.

    uv run python drlg_small.py [--mutation-check]

Runs game.exe's own code for FUN_00642380 (warp coordinates of a room),
FUN_00642390 (state flags into rooms), FUN_00642230 (Horadric staff tomb
level), FUN_00642750 (level type), FUN_00642a00 (act flag 1),
FUN_00642bb0 (find a level by id, else allocate), FUN_00642630 (room of a
coordinate), FUN_0066baa0 (waypoint flags), FUN_0066bab0 (level id of a
room), FUN_0066b4c0 (detach a room), FUN_0066bfa0 (preset units, taken once),
FUN_0066bfc0 (set +0x30), FUN_0066b980 and FUN_0066b9d0 (point in
coordinates, the second one upper bound inclusive), FUN_0066ee70 (room
initializer by preset type), FUN_0066eea0 (allocate the room tile grid),
FUN_00666120 (move a map AI pointer), FUN_00666ec0 (set the picked file) and
FUN_0067c570 (grid flags at x, y) against hand-written models on random
records (_dungeon_rig.py): return value, callee log, the whole arena, the
blocks the allocator handed out, the fatal-error line and the bytes `ret n`
pops. FUN_00642630 runs FUN_0066b980 for real; the CRT memset runs for real.
Hooked, logged, not claimed: FUN_00642ae0, FUN_0061e470, FUN_0066f1a0,
FUN_00666ac0, FUN_0067d710 and the allocator. `--mutation-check` breaks the
model eight ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt, s32

MUT = set()
TILE_FILE = 0x6E_F540       # ".\\DRLG\\RoomTile.cpp"


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def put(img, off, value):
    """Write a dword at arena offset `off` of the image."""
    img[off:off + 4] = (value & M32).to_bytes(4, "little")


def coin(rng, p=0.5):
    """True with probability p."""
    return rng.random() < p


def coord(rng):
    """A small coordinate, sometimes an extreme (the sums wrap)."""
    if coin(rng, 0.12):
        return rng.choice((0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x7FFFFFFE)) & M32
    return rng.randrange(-3, 7) & M32


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig):
        """Hook the callees this oracle leaves unclaimed."""
        self.rig = rig
        self.base = rig.arena
        rig.callee("c642ae0", 0x64_2AE0, 0, ("ecx", "edx"))
        rig.callee("c61e470", 0x61_E470, 0, ("ecx",))
        rig.callee("c66f1a0", 0x66_F1A0, 0, ("ecx", "edx"))
        rig.callee("c666ac0", 0x66_6AC0, 0, ("ecx",))
        rig.callee("c67d710", 0x67_D710, 0, ("ecx",))
        rig.custom["c61e470"] = lambda e: self.base + 0x200

    def at(self, off):
        """The address of arena offset `off`."""
        return self.base + off

    # ---- one-line accessors -----------------------------------------------------------
    def accessors(self):
        """FUN_00642380, 00642230, 00642750, 00642a00, 0066baa0, 0066bab0, 0066bfc0, 00666ec0."""
        rig, at = self.rig, self.at

        def room_level(img, rng):
            """A room at 0 whose level pointer is null or another record."""
            put(img, 0x58, at(0x300))
            return {"ecx": at(0)}
        rig.run("FUN_00642380", 0x642380, room_level, lambda m, a: m.r(a["ecx"] + 0x58) + 0x1E0, 0)

        def drlg(img, rng):
            """A DRLG record or null."""
            return {"ecx": 0 if coin(rng, 0.25) else at(0)}
        rig.run("FUN_00642230", 0x642230, drlg,
                lambda m, a: 0 if a["ecx"] == 0 and not mut("staff-null") else m.r(a["ecx"] + 0x94), 0)

        def m_flag(m, a):
            """Fatal 0x42e on null, else dwFlags & 1."""
            if a["ecx"] == 0:
                raise Halt(0x42E)
            return m.r(a["ecx"] + 0x8C) & 1
        rig.run("FUN_00642a00", 0x642A00, drlg, m_flag, 0)

        def b_type(img, rng):
            """A level id."""
            return {"stack": [rng.randrange(0, 140)]}

        def m_type(m, a):
            """FUN_0061e470's record's +0x34."""
            return m.r(m.call("c61e470", a["stack"][0]) + 0x34)
        rig.run("FUN_00642750", 0x642750, b_type, m_type, 1)

        def room(img, rng):
            """A room record."""
            put(img, 0x28, rng.choice((0x30000, 0x10000, 0x20000, 0, rng.getrandbits(32))))
            put(img, 0x58, at(0x300))
            return {"ecx": at(0)}
        rig.run("FUN_0066baa0", 0x66BAA0, room,
                lambda m, a: m.r(a["ecx"] + 0x28) & (0x10000 if mut("wp-mask") else 0x30000), 0)
        rig.run("FUN_0066bab0", 0x66BAB0, room, lambda m, a: m.r(m.r(a["ecx"] + 0x58) + 0x1D0), 0)

        def m_set30(m, a):
            """+0x30 = edx."""
            m.w(a["ecx"] + 0x30, a["edx"])
        rig.run("FUN_0066bfc0", 0x66BFC0, lambda img, rng: {"ecx": at(0), "edx": rng.getrandbits(32)}, m_set30, 0)

        def m_pick(m, a):
            """+4 = nPickedFile, returned."""
            m.w(a["ecx"] + 4, a["edx"])
            return a["edx"]
        rig.run("FUN_00666ec0", 0x666EC0, lambda img, rng: {"ecx": at(0), "edx": rng.getrandbits(32)}, m_pick, 0)

    # ---- rooms, levels, coordinates ---------------------------------------------------
    def geometry(self):
        """FUN_0066b980, FUN_0066b9d0, FUN_0067c570."""
        rig, at = self.rig, self.at

        def b_point(img, rng):
            """A coordinate record {x, y, w, h} and a point near it."""
            x0, y0, w, h = rng.randrange(-2, 4), rng.randrange(-2, 4), rng.randrange(0, 4), rng.randrange(0, 4)
            if coin(rng, 0.15):
                x0, y0, w, h = coord(rng), coord(rng), coord(rng), coord(rng)
            for o, v in zip((0, 4, 8, 12), (x0, y0, w, h)):
                put(img, o, v)

            def near(lo, ext):
                """A value from one below the low edge to one past the far edge (or an extreme)."""
                if 0 <= s32(ext) < 8 and -8 < s32(lo) < 8:
                    return (s32(lo) + rng.randrange(-1, s32(ext) + 2)) & M32
                return coord(rng)
            return {"ecx": at(0), "edx": near(x0, w), "stack": [near(y0, h)]}

        def model_point(upper_inclusive):
            """The half-open (or closed) signed rectangle test."""
            def m(mm, a):
                """x >= x0 and y >= y0 and x < or <= x0+w and y < or <= y0+h, all signed."""
                x0, y0, w, h = (mm.r(a["ecx"] + o) for o in (0, 4, 8, 12))
                x, y = a["edx"], a["stack"][0]
                if s32(x) < s32(x0) or s32(y) < s32(y0):
                    return 0
                hi = (lambda a_, b_: s32(a_) <= s32(b_)) if upper_inclusive else (lambda a_, b_: s32(a_) < s32(b_))
                if not hi(x, w + x0):
                    return 0
                return int(hi(y, h + y0))
            return m
        rig.run("FUN_0066b980", 0x66B980, b_point,
                model_point(mut("inside-inclusive")), 1)
        rig.run("FUN_0066b9d0", 0x66B9D0, b_point, model_point(not mut("closed-exclusive")), 1)

        def b_grid(img, rng):
            """A grid {cells, rows}: rows offsets and flag cells."""
            put(img, 0, at(0x800))
            put(img, 4, at(0x600))
            for y in range(8):
                put(img, 0x600 + 4 * y, rng.randrange(0, 30))
            return {"ecx": at(0), "edx": rng.randrange(0, 20), "stack": [rng.randrange(0, 8)]}
        rig.run("FUN_0067c570", 0x67C570, b_grid,
                lambda m, a: m.r(m.r(a["ecx"]) + 4 * (m.r(m.r(a["ecx"] + 4) + 4 * a["stack"][0]) + a["edx"])), 1)

        def b_room_of(img, rng):
            """A level with a chain of 0..4 rooms (rectangles near 0..9)."""
            null_level = coin(rng, 0.1)
            n = rng.randrange(0, 5)
            put(img, 0x10, at(0x400) if n else 0)
            for i in range(n):
                put(img, 0x400 + 0x80 * i + 0x24, at(0x400 + 0x80 * (i + 1)) if i + 1 < n else 0)
                for j, o in enumerate((0x34, 0x38, 0x3C, 0x40)):
                    put(img, 0x400 + 0x80 * i + o, rng.randrange(0, 6) if j < 2 else rng.randrange(1, 5))
            return {"ecx": 0 if null_level else at(0), "edx": rng.randrange(0, 9), "stack": [rng.randrange(0, 9)]}

        def m_room_of(m, a):
            """Fatal 0x2fd on a null level, 0x2fe on no rooms; else the first room containing (x, y)."""
            lvl = a["ecx"]
            if lvl == 0:
                raise Halt(0x2FD)
            r = m.r(lvl + 0x10)
            if r == 0:
                raise Halt(0x2FE)
            x, y = a["edx"], a["stack"][0]
            while r:
                x0, y0, w, h = (m.r(r + o) for o in (0x34, 0x38, 0x3C, 0x40))
                if x0 <= x < x0 + w and y0 <= y < y0 + h:
                    return r
                r = m.r(r + 0x24)
            return 0
        rig.run("FUN_00642630", 0x642630, b_room_of, m_room_of, 1)

    # ---- records that are changed or allocated ---------------------------------------
    def mutating(self):
        """FUN_00642390, 00642bb0, 0066b4c0, 0066bfa0, 0066ee70, 0066eea0, 00666120."""
        rig, at = self.rig, self.at

        def b_state(img, rng):
            """A level with a chain of rooms and a flag array."""
            n = rng.randrange(0, 6)
            put(img, 8, rng.choice((0, 0, n, n or 1)))
            put(img, 0x22C, 0 if coin(rng, 0.2) else at(0x1000))
            put(img, 0x10, at(0x400) if n else 0)
            for i in range(n):
                put(img, 0x400 + 0x80 * i + 0x24, at(0x400 + 0x80 * (i + 1)) if i + 1 < n else 0)
                put(img, 0x400 + 0x80 * i + 0x60, rng.choice((0, 2, 0x80000000, rng.getrandbits(32))))
            for i in range(8):
                put(img, 0x1000 + 4 * i, rng.choice((0, 0, 1, 0x100)))
            return {"edx": at(0)}

        def m_state(m, a):
            """When the count and the array are set, OR 1 into every room whose array entry is nonzero (the array is walked with the room chain)."""
            lv = a["edx"]
            arr, room = m.r(lv + 0x22C), m.r(lv + 0x10)
            if m.r(lv + 8) == 0 or arr == 0 or room == 0:
                return None
            off = 0
            while room:
                if m.r(arr + off) != 0 and not mut("state-skip"):
                    m.w(room + 0x60, m.r(room + 0x60) | 1)
                off += 4
                room = m.r(room + 0x24)
            return None
        rig.run("FUN_00642390", 0x642390, b_state, m_state, 0)

        def b_find(img, rng):
            """A DRLG record with a chain of 0..4 levels and an id to look for."""
            n = rng.randrange(0, 5)
            put(img, 0x47C, at(0x600) if n else 0)
            ids = [rng.randrange(0, 8) for _ in range(n)]
            for i in range(n):
                put(img, 0x600 + 0x200 * i + 0x1AC, at(0x600 + 0x200 * (i + 1)) if i + 1 < n else 0)
                put(img, 0x600 + 0x200 * i + 0x1D0, ids[i])
            return {"stack": [at(0), rng.randrange(0, 8)]}

        def m_find(m, a):
            """The first level with the id, else FUN_00642ae0(drlg, id) (ecx, edx)."""
            drlg, lid = a["stack"]
            lv = m.r(drlg + 0x47C)
            while lv:
                if m.r(lv + 0x1D0) == lid:
                    return lv
                lv = m.r(lv + 0x1AC)
            return m.call("c642ae0", drlg, lid)
        rig.run("FUN_00642bb0", 0x642BB0, b_find, m_find, 2)

        def b_detach(img, rng):
            """A room with random flags; edx and the flag argument random."""
            put(img, 0x28, rng.choice((0x100000, 0x100000 | rng.getrandbits(32), 0, rng.getrandbits(32) & ~0x100000)))
            return {"ecx": at(0), "edx": rng.choice((0, 1, rng.getrandbits(32))), "stack": [rng.getrandbits(32)]}

        def m_detach(m, a):
            """+0x30 = 0, +0x60 = flags & 1; with bit 0x100000 set, FUN_0066f1a0(room, edx == 0)."""
            r = a["ecx"]
            m.w(r + 0x30, 0)
            m.w(r + 0x60, a["stack"][0] & 1)
            if m.r(r + 0x28) & 0x100000:
                m.call("c66f1a0", r, int((a["edx"] == 0) != mut("detach-arg")))
            return None
        rig.run("FUN_0066b4c0", 0x66B4C0, b_detach, m_detach, 1)

        def b_units(img, rng):
            """A room with random flags, bits 0x40000 and 0x4000000 often set or clear."""
            f = rng.getrandbits(32) & ~0x4040000
            f |= rng.choice((0, 0x40000, 0x4000000, 0x4040000))
            put(img, 0x28, f)
            return {"ecx": at(0)}

        def m_units(m, a):
            """Returns +0x5c, but only once per room unless 0x40000: the first call sets 0x4000000, later ones get null."""
            r = a["ecx"]
            f = m.r(r + 0x28)
            if not f & 0x40000:
                if f & 0x4000000:
                    return 0
                m.w(r + 0x28, f | (0 if mut("units-noset") else 0x4000000))
            return m.r(r + 0x5C)
        rig.run("FUN_0066bfa0", 0x66BFA0, b_units, m_units, 0)

        def b_init(img, rng):
            """A room with preset type 0, 1, 2 or other."""
            put(img, 0x48, rng.choice((0, 1, 2, 3, rng.getrandbits(32))))
            put(img, 0x28, rng.getrandbits(32))
            return {"ecx": at(0)}

        def m_init(m, a):
            """Type 1: FUN_0067d710; type 2: FUN_00666ac0; always |= 0x100000."""
            r = a["ecx"]
            t = m.r(r + 0x48)
            if t == 1:
                m.call("c67d710", r)
            elif t == 2:
                m.call("c666ac0", r)
            m.w(r + 0x28, m.r(r + 0x28) | 0x100000)
            return None
        rig.run("FUN_0066ee70", 0x66EE70, b_init, m_init, 0)

        def b_tiles(img, rng):
            """A room with or without a tile grid, and the chain room -> level -> DRLG -> pool."""
            put(img, 0x54, 0 if coin(rng, 0.6) else at(0x700))
            put(img, 0x58, at(0x200))
            put(img, 0x200 + 0x1B4, at(0x600))
            return {"ecx": at(0)}

        def m_tiles(m, a):
            """When +0x54 is null: allocate 0x2c bytes from the DRLG's pool (RoomTile.cpp line 0x533), memset 0 it, store it."""
            r = a["ecx"]
            if m.r(r + 0x54) == 0:
                pool = m.r(m.r(m.r(r + 0x58) + 0x1B4) + 0x478)
                p = m.alloc(pool, 0x2C, 0x533, TILE_FILE)
                m.w(r + 0x54, p if not mut("tile-realloc") else p + 4)
            return None

        def dirty(e):
            """An allocator that returns dirty memory, so the memset is visible."""
            from unicorn.x86_const import UC_X86_REG_EDX
            size = max(e.mu.reg_read(UC_X86_REG_EDX), 1)
            p = e.alloc(size)
            e.mu.mem_write(p, b"\xAA" * size)
            return p
        rig.custom["alloc"] = dirty
        rig.run("FUN_0066eea0", 0x66EEA0, b_tiles, m_tiles, 0)
        del rig.custom["alloc"]

        def b_move(img, rng):
            """Two pointer cells, sometimes the same cell."""
            put(img, 0, rng.getrandbits(32))
            put(img, 8, rng.getrandbits(32))
            return {"ecx": at(0), "edx": at(0) if coin(rng, 0.1) else at(8)}

        def m_move(m, a):
            """*dst = *src; *src = 0; return *dst."""
            src, dst = a["ecx"], a["edx"]
            m.w(dst, m.r(src))
            m.w(src, 0)
            return m.r(dst)
        rig.run("FUN_00666120", 0x666120, b_move, m_move, 0)


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 80
    s = Suite(rig)
    s.accessors()
    s.geometry()
    s.mutating()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("staff-null", "wp-mask", "inside-inclusive", "closed-exclusive", "state-skip", "detach-arg",
                 "units-noset", "tile-realloc")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "small DRLG helper", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
