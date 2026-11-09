# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 17 small DRLG helpers (Preset, DrlgRoom, DrlgLogic, Maze, packed-point tables) in stock game.exe 1.14d.

    uv run python drlg_misc.py [--mutation-check]

Runs game.exe's own code for FUN_00666130, FUN_0066c620, FUN_0066c660,
FUN_0066cf30, FUN_0066d410, FUN_00670970, FUN_00678d70, FUN_00678db0,
FUN_00678df0, FUN_0067a5d0, FUN_0067a5f0, FUN_0067b9c0, FUN_0067bbc0,
FUN_0067d6d0, FUN_0067d7a0 and FUN_0067dce0 against hand-written models on
random records (_dungeon_rig.py): return value, callee log, arena, allocator
blocks, fatal-error line and the bytes `ret n` pops. Tables in game.exe's data
are read from the emulator. The CRT memset runs for real, with an allocator
that returns dirty memory. Hooked, logged, not claimed: FUN_0067c570,
FUN_0066d3b0, FUN_00678b80, FUN_00678c10 and the allocator.
`--mutation-check` breaks the model three ways.
"""
import argparse
import struct
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt

MUT = set()


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
        self.flags = 0
        rig.callee("c67c570", 0x67_C570, 1, ("ecx", "edx"))
        rig.callee("c66d3b0", 0x66_D3B0, 0, ("eax",))
        rig.callee("c678b80", 0x67_8B80, 2, ("eax", "ecx"))
        rig.callee("c678c10", 0x67_8C10, 2, ())
        rig.custom["c67c570"] = lambda e: self.flags
        rig.custom["c678b80"] = lambda e: self.idx
        rig.custom["c678c10"] = lambda e: self.idx

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

    def word(self, addr):
        """A dword of game.exe's image."""
        return struct.unpack("<I", self.rig.m.read(addr, 4))[0]

    def byte(self, addr):
        """A byte of game.exe's image."""
        return self.rig.m.read(addr, 1)[0]

    def records(self):
        """FUN_00666130, 0066c620, 0066c660, 0066cf30, 0066d410, 00670970, 0067d6d0, 0067d7a0, 0067dce0."""
        rig, at = self.rig, self.at
        f_pre, f_room, f_maze = 0x6E_E7C4, 0x6E_EEF0, 0x6F_1F68

        def m_free(m, a):
            """Free the block's +4 (line 0x597) and the block (0x598)."""
            m.free(a["ecx"], m.r(a["edx"] + 4), 0x597, f_pre)
            m.free(a["ecx"], a["edx"], 0x598, f_pre)
        rig.run("FUN_00666130", 0x666130, lambda img, rng: {"ecx": rng.getrandbits(32), "edx": at(0)}, m_free, 0)

        def b_tile(img, rng):
            """A room with an optional record (+0x64) whose first byte's bit 0 may be set."""
            put(img, 0x64, rng.choice((0, at(0x300))))
            put(img, 0x300, rng.choice((0, 1, 2, 3)))
            self.flags = rng.getrandbits(32)
            return {"ecx": at(0), "edx": at(0x200), "stack": [rng.randrange(0, 20), rng.randrange(0, 20)]}

        def m_tile(m, a):
            """When the record exists and its bit 0 is clear: FUN_0067c570(rec + 0x1c, x - room.x, y - room.y); store at out +0x10 (0 otherwise)."""
            r, out = a["ecx"], a["edx"]
            rec = m.r(r + 0x64)
            v = 0
            if rec and not m.r(rec) & 1:
                v = m.call("c67c570", rec + 0x1C, (a["stack"][0] - m.r(r + 0x34)) & M32, (a["stack"][1] - m.r(r + 0x38)) & M32)
            m.w(out + 0x10, v)
        rig.run("FUN_0066c620", 0x66C620, b_tile, m_tile, 2)

        def m_new(m, a):
            """Allocate 0x34 (DrlgLogic line 0xdf), zero it, store at room +0x64, return it."""
            p = m.alloc(a["ecx"], 0x34, 0xDF, f_room)
            m.w(a["stack"][0] + 0x64, p)
            return p
        rig.run("FUN_0066c660", 0x66C660, lambda img, rng: {"ecx": rng.getrandbits(32), "stack": [at(0)]}, m_new, 1)

        def b_rec(img, rng):
            """A room whose record (+0x64) may be null."""
            put(img, 0x64, rng.choice((0, at(0x300))))
            return {"ecx": at(0)}

        def m_rec(m, a):
            """Fatal 0x2cd without the record, else its +0x30."""
            rec = m.r(a["ecx"] + 0x64)
            if rec == 0:
                raise Halt(0x2CD)
            return m.r(rec + 0x30)
        rig.run("FUN_0066cf30", 0x66CF30, b_rec, m_rec, 0)

        def b_each(img, rng):
            """A room with 0..4 near rooms."""
            n = rng.randrange(0, 5)
            put(img, 0x2C, n)
            put(img, 8, at(0x300))
            for i in range(n):
                put(img, 0x300 + 4 * i, rng.getrandbits(32))
            return {"ecx": at(0)}

        def m_each(m, a):
            """FUN_0066d3b0(EAX = near room) for each near room."""
            for i in range(m.r(a["ecx"] + 0x2C)):
                m.call("c66d3b0", m.r(m.r(a["ecx"] + 8) + 4 * i))
        rig.run("FUN_0066d410", 0x66D410, b_each, m_each, 0)

        def b_set(img, rng):
            """An object (EAX) with a record (+0x20); value in EDX, a, flag."""
            put(img, 0x20, at(0x300))
            put(img, 0x300 + 0xC, rng.getrandbits(32))
            return {"regs": {"eax": at(0)}, "edx": rng.getrandbits(32), "stack": [rng.getrandbits(32), rng.choice((0, 1, 7))]}

        def m_set(m, a):
            """rec = {edx, a}; flag nonzero clears bit 1 of +0xc, zero sets it."""
            rec = m.r(a["regs"]["eax"] + 0x20)
            m.w(rec, a["edx"])
            m.w(rec + 4, a["stack"][0])
            f = m.r(rec + 0xC)
            m.w(rec + 0xC, (f & ~2) if a["stack"][1] else (f | (0 if mut("set-bit") else 2)))
        rig.run("FUN_00670970", 0x670970, b_set, m_set, 2)

        def m_maze(m, a):
            """Allocate 0x70 from the act's pool (line 0xc5), zero it, store at +0x20."""
            r = a["ecx"]
            m.w(r + 0x20, m.alloc(m.r(m.r(m.r(r + 0x58) + 0x1B4) + 0x478), 0x70, 0xC5, f_maze))

        def b_room(img, rng):
            """A room whose level (+0x58) is at 0x300 and act at 0x600."""
            put(img, 0x58, at(0x300))
            put(img, 0x300 + 0x1B4, at(0x600))
            return {"ecx": at(0)}
        rig.run("FUN_0067d6d0", 0x67D6D0, b_room, m_maze, 0)

        def b_f(img, rng):
            """An object with a record (+0x20) whose +0x54 has random bits; a level id at +0x1d0."""
            put(img, 0x20, at(0x300))
            put(img, 0x300 + 0x54, rng.getrandbits(32))
            put(img, 0x1D0, rng.choice((0x75, 0x75, 3)))
            return {"ecx": at(0)}
        rig.run("FUN_0067d7a0", 0x67D7A0, b_f, lambda m, a: m.r(m.r(a["ecx"] + 0x20) + 0x54) & 0x80, 0)
        rig.run("FUN_0067dce0", 0x67DCE0, b_f, lambda m, a: 4 + int(m.r(a["ecx"] + 0x1D0) == 0x75), 0)

    def tables(self):
        """FUN_00678d70, 00678db0, 00678df0, 0067a5d0, 0067a5f0, 0067b9c0, 0067bbc0."""
        rig, at = self.rig, self.at

        def b_idx(img, rng):
            """A hooked index and the stack arguments (a, b), ECX the output."""
            self.idx = rng.randrange(0, 12)
            return {"ecx": at(0x300), "edx": rng.getrandbits(32), "stack": [rng.getrandbits(32), rng.getrandbits(32)]}

        def m_dwords(m, a):
            """FUN_00678c10(a, b) -> index; out = the three dwords at 0x6f1518 + 12 * index."""
            idx = m.call("c678c10", a["stack"][0], a["stack"][1])
            for k in range(3):
                m.w(a["ecx"] + 4 * k, self.word(0x6F_1518 + 12 * idx + 4 * k))
        rig.run("FUN_00678d70", 0x678D70, b_idx, m_dwords, 2)

        def m_bytes(m, a):
            """FUN_00678c10(a, b) -> index; out = the three bytes at 0x6f1648 + 3 * index, as dwords."""
            idx = m.call("c678c10", a["stack"][0], a["stack"][1])
            for k in range(3):
                m.w(a["ecx"] + 4 * k, self.byte(0x6F_1648 + 3 * idx + k))
        rig.run("FUN_00678df0", 0x678DF0, b_idx, m_bytes, 2)

        def b_out(img, rng):
            """As above plus an output pointer on the stack."""
            args = b_idx(img, rng)
            args["stack"].append(at(0x300))
            return args

        def m_bytes_b(m, a):
            """FUN_00678b80(eax = a, ecx = b; stack ecx, edx) -> index; out = the three bytes at 0x6f1648 + 3 * index."""
            idx = m.call("c678b80", a["stack"][0], a["stack"][1], a["ecx"], a["edx"])
            for k in range(3):
                m.w(a["stack"][2] + 4 * k, self.byte(0x6F_1648 + 3 * idx + k + (1 if mut("tab-off") else 0)))
        rig.run("FUN_00678db0", 0x678DB0, b_out, m_bytes_b, 3)

        def b_dir(img, rng):
            """EAX and the stack value."""
            return {"regs": {"eax": rng.getrandbits(32)}, "stack": [rng.getrandbits(32)]}
        rig.run("FUN_0067a5d0", 0x67A5D0, b_dir,
                lambda m, a: (((a["regs"]["eax"] - a["stack"][0]) & 7) << 5) + 0x6F_17D8, 1)

        # The object is 0x6280 bytes; the arena (0x4000) is smaller, so this one is run on the image's own memory.
        e = rig.m
        obj = e.alloc(0x6300)

        def run_box(rng):
            """Check FUN_0067a5f0 against the model on 400 random boxes and points."""
            for _ in range(400):
                x0, y0 = rng.randrange(0, 10), rng.randrange(0, 10)
                x1, y1 = x0 + rng.randrange(0, 6), y0 + rng.randrange(0, 6)
                e.mu.mem_write(obj + 0x6278, struct.pack("<4H", x0, y0, x1, y1))
                px, py = rng.randrange(0, 18), rng.randrange(0, 18)
                p = px | (py << 16)
                got = e.call(0x67A5F0, p, ecx=obj, limit=100000)
                want = int(x0 <= px <= x1 and y0 <= py <= y1 + (1 if mut("box-y") else 0))
                rig_mod.require(got == want, f"FUN_0067a5f0 {x0, y0, x1, y1} {px, py}: {got}, model {want}")
                rig.cases += 1
        run_box(rig.rng)

        def b_pk(img, rng):
            """Two packed points."""
            def pk():
                """A packed point."""
                return rng.randrange(0, 30) | (rng.randrange(0, 30) << 16)
            return {"stack": [pk(), pk()]}

        def m_sq(m, a):
            """dx = lo(a) - lo(b), dy = hi(a) - hi(b); dx * dx + dy * dy."""
            p, q = a["stack"]
            dx, dy = (p & 0xFFFF) - (q & 0xFFFF), (p >> 16) - (q >> 16)
            return (dx * dx + dy * dy) & M32
        rig.run("FUN_0067b9c0", 0x67B9C0, b_pk, m_sq, 2)

        def b_pk2(img, rng):
            """Packed points with coordinates 0..2."""
            def pk():
                """A packed point."""
                return rng.randrange(0, 3) | (rng.randrange(0, 3) << 16)
            return {"stack": [pk(), pk()]}

        def m_cell(m, a):
            """Index 3 * (hi(b) - hi(a)) - lo(a) + lo(b) into the dwords at 0x6f1e28."""
            p, q = a["stack"]
            idx = 3 * ((q >> 16) - (p >> 16)) - (p & 0xFFFF) + (q & 0xFFFF)
            return self.word(0x6F_1E28 + 4 * idx)
        rig.run("FUN_0067bbc0", 0x67BBC0, b_pk2, m_cell, 2)


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 80
    s = Suite(rig)
    s.records()
    s.tables()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("set-bit", "tab-off", "box-y")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "small DRLG helper (records and tables)", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
