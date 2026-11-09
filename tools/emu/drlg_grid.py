# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the DRLG grid helpers of DRLG\\DrlgGrid.cpp in stock game.exe 1.14d.

    uv run python drlg_grid.py [--mutation-check]

A grid is {cells +0, row offsets +4, width +8, height +0xc, flag +0x10}.
Runs game.exe's own code for FUN_0067c430 (OR into a cell word), FUN_0067c480
(grid has cells), FUN_0067c4a0 (point in bounds), FUN_0067c4d0 and
FUN_0067c550 (cell address), FUN_0067c4f0 and FUN_0067c520 (apply a table
op to a cell), FUN_0067c6d0 (apply an op along the bottom row and right
column), FUN_0067c890 (apply op 0 at each listed cell), FUN_0067cb80,
FUN_0067cbf0, FUN_0067cc60 (build a grid), FUN_0067ccc0 and FUN_0067cd30
(a grid over a rectangle of another), FUN_0067cd90 (free the row table),
FUN_0067cdc0 (clear), FUN_0067cdd0 (allocate a 0x14-byte record) and
FUN_0067ce00 (set flag bits) against hand-written models on random records
(_dungeon_rig.py): return value, callee log, arena, allocator blocks and the
bytes `ret n` pops. The CRT memset runs for real, against an allocator that
returns dirty memory. The op table at 0x749bac is pointed at hooked stubs.
`--mutation-check` breaks the model four ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt, s32  # noqa: F401

MUT = set()
FILE = 0x6F_1F40
FILE2 = 0x6F_1F54
OPS = (0x40_2010, 0x40_2020, 0x40_2030, 0x40_2040)


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def put(img, off, value):
    """Write a dword at arena offset `off`."""
    img[off:off + 4] = (value & M32).to_bytes(4, "little")


class Suite:
    """Builds and runs the grid cases."""

    def __init__(self, rig):
        """Point the op table at hooked stubs."""
        self.rig = rig
        self.base = rig.arena
        for i, addr in enumerate(OPS):
            rig.callee(f"op{i}", addr, 0, ("ecx", "edx"))
            rig.m.w32(0x74_9BAC + 4 * i, addr)

    def at(self, off):
        """Arena address of offset `off`."""
        return self.base + off

    def grid(self, img, rng, w=None, h=None):
        """A grid at 0: cells at 0x800, rows at 0x600 (row i at i * w)."""
        w = rng.randrange(1, 6) if w is None else w
        h = rng.randrange(1, 6) if h is None else h
        for o, v in ((0, self.at(0x800)), (4, self.at(0x600)), (8, w), (0xC, h)):
            put(img, o, v)
        for i in range(h):
            put(img, 0x600 + 4 * i, i * w)
        return w, h

    def access(self):
        """FUN_0067c430, 0067c480, 0067c4a0, 0067c4d0, 0067c550, 0067c4f0, 0067c520, 0067c6d0, 0067c890."""
        rig, at = self.rig, self.at

        rig.run("FUN_0067c430", 0x67C430, lambda img, rng: {"ecx": at(0x10), "edx": rng.getrandbits(32)},
                lambda m, a: m.w(a["ecx"], m.r(a["ecx"]) | (0 if mut("or-and") else a["edx"])), 0)

        def b_has(img, rng):
            """Null, a zero cell pointer or a set one."""
            put(img, 0, rng.choice((0, 0, at(0x800))))
            return {"ecx": rng.choice((0, at(0)))}
        rig.run("FUN_0067c480", 0x67C480, b_has,
                lambda m, a: int(a["ecx"] != 0 and m.r(a["ecx"]) != 0), 0)

        def b_in(img, rng):
            """A grid and a point near it."""
            w, h = self.grid(img, rng)
            return {"ecx": at(0), "edx": rng.randrange(-1, w + 1) & M32, "stack": [rng.randrange(-1, h + 1) & M32]}
        rig.run("FUN_0067c4a0", 0x67C4A0, b_in,
                lambda m, a: int(0 <= s32(a["edx"]) < s32(m.r(a["ecx"] + 8)) and 0 <= s32(a["stack"][0]) < s32(m.r(a["ecx"] + 0xC))), 1)

        def b_cell(img, rng):
            """A grid and an in-range cell."""
            w, h = self.grid(img, rng)
            return {"ecx": at(0), "edx": rng.randrange(0, w), "stack": [rng.randrange(0, h)]}
        rig.run("FUN_0067c550", 0x67C550, b_cell,
                lambda m, a: m.r(a["ecx"]) + 4 * (m.r(m.r(a["ecx"] + 4) + 4 * a["stack"][0]) + a["edx"]), 1)

        def b_cell_eax(img, rng):
            """The same with the grid in EAX, y in EDX, x on the stack."""
            w, h = self.grid(img, rng)
            return {"regs": {"eax": at(0)}, "edx": rng.randrange(0, h), "stack": [rng.randrange(0, w)]}
        rig.run("FUN_0067c4d0", 0x67C4D0, b_cell_eax,
                lambda m, a: m.r(a["regs"]["eax"]) + 4 * (m.r(m.r(a["regs"]["eax"] + 4) + 4 * a["edx"]) + a["stack"][0]), 1)

        def b_op(img, rng):
            """A grid, a cell, a value, an op index."""
            w, h = self.grid(img, rng)
            return {"ecx": at(0), "edx": rng.randrange(0, w),
                    "stack": [rng.randrange(0, h), rng.getrandbits(32), rng.randrange(0, 4)]}

        def m_op(m, a):
            """op[k](&cell(x, y), value)."""
            y, v, k = a["stack"]
            m.call(f"op{k}", m.r(a["ecx"]) + 4 * (m.r(m.r(a["ecx"] + 4) + 4 * y) + a["edx"]), v)
        rig.run("FUN_0067c4f0", 0x67C4F0, b_op, m_op, 3)

        def b_op2(img, rng):
            """A grid, a point record {x, y} at 0x40, a value, an op index."""
            w, h = self.grid(img, rng)
            put(img, 0x40, rng.randrange(0, w))
            put(img, 0x44, rng.randrange(0, h))
            return {"ecx": at(0), "edx": at(0x40), "stack": [rng.getrandbits(32), rng.randrange(0, 4)]}

        def m_op2(m, a):
            """op[k](&cell(point), value)."""
            v, k = a["stack"]
            p = a["edx"]
            m.call(f"op{k}", m.r(a["ecx"]) + 4 * (m.r(m.r(a["ecx"] + 4) + 4 * m.r(p + 4)) + m.r(p)), v)
        rig.run("FUN_0067c520", 0x67C520, b_op2, m_op2, 2)

        def b_border(img, rng):
            """A grid, a value, an op index."""
            self.grid(img, rng)
            return {"ecx": at(0), "edx": rng.getrandbits(32), "stack": [rng.randrange(0, 4)]}

        def m_border(m, a):
            """op on the first w - 1 cells of the last row, then on the last column of every row."""
            g, v, k = a["ecx"], a["edx"], a["stack"][0]
            cells, rows, w, h = (m.r(g + o) for o in (0, 4, 8, 0xC))
            p = cells + 4 * m.r(rows + 4 * (h - 1))
            for _ in range(max(w - 1, 0)):
                m.call(f"op{k}", p, v)
                p += 4
            for j in range(h):
                m.call(f"op{k}", cells + 4 * (m.r(rows + 4 * j) + w - (0 if mut("border-col") else 1)), v)
        rig.run("FUN_0067c6d0", 0x67C6D0, b_border, m_border, 1)

        def b_list(img, rng):
            """A grid and a list of 0..4 nodes {x, y, ..., next +0x10}, some out of range."""
            w, h = self.grid(img, rng)
            n = rng.randrange(0, 5)
            for i in range(n):
                put(img, 0x300 + 0x40 * i, rng.randrange(-1, w + 1))
                put(img, 0x304 + 0x40 * i, rng.randrange(-1, h + 1))
                put(img, 0x310 + 0x40 * i, at(0x300 + 0x40 * (i + 1)) if i + 1 < n else 0)
            return {"ecx": at(0), "edx": at(0x300) if n else 0, "stack": [rng.getrandbits(32)]}

        def m_list(m, a):
            """op[0](&cell, value) for each node inside the grid."""
            g, node = a["ecx"], a["edx"]
            cells, rows, w, h = (m.r(g + o) for o in (0, 4, 8, 0xC))
            while node:
                x, y, nxt = s32(m.r(node)), s32(m.r(node + 4)), m.r(node + 0x10)
                if 0 <= x < s32(w) and 0 <= y < s32(h):
                    m.call("op0", cells + 4 * (m.r(rows + 4 * y) + x), a["stack"][0])
                node = nxt
        rig.run("FUN_0067c890", 0x67C890, b_list, m_list, 1)

    def build(self):
        """FUN_0067cb80, 0067cbf0, 0067cc60, 0067ccc0, 0067cd30, 0067cd90, 0067cdc0, 0067cdd0, 0067ce00."""
        rig, at = self.rig, self.at

        def dirty(e):
            """An allocator that returns dirty memory, so memsets are visible."""
            from unicorn.x86_const import UC_X86_REG_EDX
            size = max(e.mu.reg_read(UC_X86_REG_EDX), 1)
            p = e.alloc(size)
            e.mu.mem_write(p, b"\xAA" * size)
            return p
        rig.custom["alloc"] = dirty

        def sized(img, rng):
            """Width and height, pool in ECX, the grid record at 0x10."""
            return rng.randrange(0, 6), rng.randrange(0, 6)

        def b_new(img, rng):
            """FUN_0067cb80 arguments."""
            w, h = sized(img, rng)
            return {"ecx": rng.getrandbits(32), "edx": at(0x10), "stack": [w, h]}

        def m_new(m, a):
            """One block of (w + 1) * h dwords: row offsets, then cells (zeroed); sets +0x10 to 0."""
            w, h = a["stack"]
            g = a["edx"]
            size = 4 * (w + 1) * h
            p = m.alloc(a["ecx"], size, 0x197, FILE)
            m.w(g + 8, w)
            m.w(g + 0xC, h)
            m.w(g + 4, p)
            m.w(g, p + 4 * h)
            for i in range(h):
                m.w(p + 4 * i, i * w + (1 if mut("rows-bias") else 0))
            m.w(g + 0x10, 0)
        rig.run("FUN_0067cb80", 0x67CB80, b_new, m_new, 2)

        def b_over(img, rng):
            """FUN_0067cbf0: grid at 0x10, width in EDX, then height, cells, rows."""
            w, h = sized(img, rng)
            return {"ecx": at(0x10), "edx": w, "stack": [h, at(0x800), at(0x600)]}

        def m_over(m, a):
            """cells zeroed for w * h dwords, rows[i] = i * w, sets +0x10 to 0."""
            w, (h, cells, rows) = a["edx"], a["stack"]
            g = a["ecx"]
            m.w(g + 8, w)
            m.w(g + 0xC, h)
            m.w(g, cells)
            for i in range(w * h):
                m.w(cells + 4 * i, 0)
            m.w(g + 4, rows)
            for i in range(h):
                m.w(rows + 4 * i, i * w)
            m.w(g + 0x10, 0)
        rig.run("FUN_0067cbf0", 0x67CBF0, b_over, m_over, 3)

        def b_rows(img, rng):
            """FUN_0067cc60: pool ECX, grid EDX, cells, width, height."""
            w, h = sized(img, rng)
            return {"ecx": rng.getrandbits(32), "edx": at(0x10), "stack": [at(0x800), w, h]}

        def m_rows(m, a):
            """Allocate h dwords (line 0x1d0) of row offsets i * w; sets +0x10 to 1."""
            cells, w, h = a["stack"]
            g = a["edx"]
            m.w(g + 8, w)
            m.w(g + 0xC, h)
            m.w(g, cells)
            p = m.alloc(a["ecx"], 4 * h, 0x1D0, FILE)
            m.w(g + 4, p)
            for i in range(h):
                m.w(p + 4 * i, i * w)
            m.w(g + 0x10, 1)
        rig.run("FUN_0067cc60", 0x67CC60, b_rows, m_rows, 3)

        def b_sub(img, rng):
            """A source rectangle {x, y, w, h} at 0x40 and a stride."""
            for o in (0, 4):
                put(img, 0x40 + o, rng.randrange(0, 4))
            for o in (8, 0xC):
                put(img, 0x40 + o, rng.randrange(0, 5))
            return {"stride": rng.randrange(1, 9)}

        def b_view(img, rng):
            """FUN_0067ccc0: pool ECX, destination EDX, base, rectangle, stride."""
            r = b_sub(img, rng)
            return {"ecx": rng.getrandbits(32), "edx": at(0x10), "stack": [at(0x800), at(0x40), r["stride"]]}

        def m_view(m, a):
            """Copy w and h; cells at base + 4 * (y * stride + x); rows i * stride (allocated, line 0x1ec); sets +0x10 to 1."""
            base, src, stride = a["stack"]
            g = a["edx"]
            w, h, x, y = m.r(src + 8), m.r(src + 0xC), m.r(src), m.r(src + 4)
            m.w(g + 8, w)
            m.w(g + 0xC, h)
            m.w(g, base + 4 * (y * stride + x))
            p = m.alloc(a["ecx"], 4 * h, 0x1EC, FILE)
            m.w(g + 4, p)
            for i in range(h):
                m.w(p + 4 * i, i * stride)
            m.w(g + 0x10, 1)
        rig.run("FUN_0067ccc0", 0x67CCC0, b_view, m_view, 3)

        def b_view2(img, rng):
            """FUN_0067cd30: destination ECX, base EDX, rectangle, stride, rows pointer."""
            r = b_sub(img, rng)
            return {"ecx": at(0x10), "edx": at(0x800), "stack": [at(0x40), r["stride"], at(0x600)]}

        def m_view2(m, a):
            """As FUN_0067ccc0 but the caller supplies the row table (not allocated)."""
            src, stride, rows = a["stack"]
            g = a["ecx"]
            w, h, x, y = m.r(src + 8), m.r(src + 0xC), m.r(src), m.r(src + 4)
            m.w(g + 8, w)
            m.w(g + 0xC, h)
            m.w(g, a["edx"] + 4 * (y * stride + x))
            m.w(g + 4, rows)
            for i in range(h):
                m.w(rows + 4 * i, i * stride)
            m.w(g + 0x10, 1)
        rig.run("FUN_0067cd30", 0x67CD30, b_view2, m_view2, 3)

        def b_free(img, rng):
            """A grid with or without a row table."""
            put(img, 4, rng.choice((0, at(0x600))))
            put(img, 0, at(0x800))
            return {"ecx": rng.getrandbits(32), "edx": at(0)}

        def m_free(m, a):
            """With a row table: free it (line 0x218); either way clear both pointers."""
            g = a["edx"]
            if m.r(g + 4):
                m.free(a["ecx"], m.r(g + 4), 0x218, FILE)
            m.w(g + 4, 0)
            m.w(g, 0)
        rig.run("FUN_0067cd90", 0x67CD90, b_free, m_free, 0)

        def m_clear(m, a):
            """Both words zero."""
            m.w(a["ecx"], 0)
            m.w(a["ecx"] + 4, 0)
        rig.run("FUN_0067cdc0", 0x67CDC0, lambda img, rng: {"ecx": at(0)}, m_clear, 0)

        def m_rec(m, a):
            """Allocate 0x14 bytes (file 0x6f1f54 line 0x13), zero them, byte +8 = dl; returns the block."""
            p = m.alloc(a["ecx"], 0x14, 0x13, FILE2)
            m.w(p + 8, a["edx"] & 0xFF, 1)
            return p
        rig.run("FUN_0067cdd0", 0x67CDD0,
                lambda img, rng: {"ecx": rng.getrandbits(32), "edx": rng.getrandbits(32)}, m_rec, 0)

        def m_flag(m, a):
            """+0xc |= 1, and |= 2 when the argument is nonzero."""
            o = a["regs"]["eax"]
            m.w(o + 0xC, m.r(o + 0xC) | 1 | (2 if a["stack"][0] and not mut("flag-two") else 0))
        rig.run("FUN_0067ce00", 0x67CE00,
                lambda img, rng: {"regs": {"eax": at(0x10)}, "stack": [rng.choice((0, 1, rng.getrandbits(32)))]},
                m_flag, 1)
        del rig.custom["alloc"]


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 80
    s = Suite(rig)
    s.access()
    s.build()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("or-and", "border-col", "rows-bias", "flag-two")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "DrlgGrid.cpp helper", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
