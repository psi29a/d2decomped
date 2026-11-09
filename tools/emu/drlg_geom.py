# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 12 DRLG geometry, direction, list-link and seed helpers in stock game.exe 1.14d.

    uv run python drlg_geom.py [--mutation-check]

Runs game.exe's own code for FUN_00666170, FUN_006661a0, FUN_0066c7f0,
FUN_0066d7f0, FUN_0066def0, FUN_00679f70, FUN_0067a390, FUN_0067a420,
FUN_0067a550, FUN_0067a630, FUN_0067d280 and FUN_0067d7e0 against
hand-written models on random records (_dungeon_rig.py): return value, callee
log, arena, allocator blocks, fatal-error line and the bytes `ret n` pops.
FUN_00643560 (scale a pair by 5) and FUN_00678f00 (halve a vector until both
parts fit 16.16) run for real inside the callers. FUN_0067a630 needs a game
object larger than the arena and is run on the emulator's own memory. Tables in
game.exe's data are read from the emulator. Hooked, logged, not claimed:
FUN_0066bf30 (a node allocator, handed arena nodes), FUN_0066dde0,
FUN_0067c4f0, FUN_00679a60, FUN_0064fd70 (writes the case's vector) and
FUN_00679530 (its result mixes the vector it was given).
`--mutation-check` breaks the model seven ways.
"""
import argparse
import struct
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt, mix, s32

MUT = set()
NODES = 0x2400


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def put(img, off, value):
    """Write a dword at arena offset `off`."""
    img[off:off + 4] = (value & M32).to_bytes(4, "little")


def abs32(v):
    """The value after the code's `test; jge; neg` (32-bit, so INT_MIN stays negative)."""
    return v if v >= 0 else s32(-v)


def clamp(x, y):
    """FUN_00678f00: halve both parts while x, then y, is outside +-0x10000 (signed compares)."""
    lim = 0x8000 if mut("clamp") else 0x10000
    if x >= 0:
        while x > lim:
            x, y = x >> 1, y >> 1
    else:
        while x < -lim:
            x, y = x >> 1, y >> 1
    if y >= 0:
        while y > lim:
            x, y = x >> 1, y >> 1
    else:
        while y < -lim:
            x, y = x >> 1, y >> 1
    return x, y


def scaled(vx, vy, scale):
    """FUN_0067a390's arithmetic: (x * scale >> 8, y * scale >> 8) and and the larger absolute value (the caller halts 0x331 from 0x200000)."""
    sh = 7 if mut("shift") else 8
    x = s32(s32(vx) * scale & M32) >> sh
    y = s32(s32(vy) * scale & M32) >> sh
    ax, ay = abs32(x), abs32(y)
    return x, y, ax if ax > ay else ay


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig):
        """Hook the callees this oracle leaves unclaimed."""
        self.rig = rig
        self.base = rig.arena
        self.node = 0
        self.vec = (0, 0)
        self.new = 0
        rig.callee("c66bf30", 0x66_BF30, 5, ("ecx", "edx"))
        rig.callee("c66dde0", 0x66_DDE0, 4, ("ecx", "edx"))
        rig.callee("c67c4f0", 0x67_C4F0, 3, ("ecx", "edx"))
        rig.callee("c679a60", 0x67_9A60, 0, ("esi",))
        rig.callee("c64fd70", 0x64_FD70, 2, ())
        rig.callee("c679530", 0x67_9530, 2, ("eax",), nlog=1)

        def next_node(e):
            """Hand out the next arena node (0x20 bytes at NODES)."""
            self.node += 1
            return self.at(NODES + 0x20 * (self.node - 1))

        def write_vec(e):
            """FUN_0064fd70 stand-in: write the case's vector at [ecx]."""
            from unicorn.x86_const import UC_X86_REG_ECX
            ecx = e.mu.reg_read(UC_X86_REG_ECX)
            e.mu.mem_write(ecx, struct.pack("<II", *self.vec))
            return 0

        def new_pos(e):
            """FUN_00679a60 stand-in: write the case's packed position at [edi]."""
            from unicorn.x86_const import UC_X86_REG_EDI
            e.mu.mem_write(e.mu.reg_read(UC_X86_REG_EDI), struct.pack("<I", self.new))
            return self.new

        def final(e):
            """FUN_00679530 stand-in: mix(eax, vector read from [ecx], first argument, second argument's value)."""
            from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX
            vx, vy = struct.unpack("<II", e.read(e.mu.reg_read(UC_X86_REG_ECX), 8))
            return mix("c679530", e.mu.reg_read(UC_X86_REG_EAX), vx, vy, e.arg(0), e.r32(e.arg(1)))
        rig.custom["c66bf30"] = next_node
        rig.custom["c64fd70"] = write_vec
        rig.custom["c679a60"] = new_pos
        rig.custom["c679530"] = final

    def at(self, off):
        """Arena address of offset `off`."""
        return self.base + off

    def word(self, addr):
        """A dword of game.exe's image."""
        return struct.unpack("<I", self.rig.m.read(addr, 4))[0]

    def lists(self):
        """FUN_00666170, 006661a0, 0066c7f0, 0066def0."""
        rig, at = self.rig, self.at

        def b_link(img, rng):
            """A room at 0x300 with a node list head (+0x38); registers and the argument random."""
            self.node = 0
            return {"regs": {"eax": rng.getrandbits(32), "esi": at(0x300)}, "ecx": rng.getrandbits(32),
                    "edx": rng.getrandbits(32), "stack": [rng.getrandbits(32)]}

        def m_link(m, a):
            """New node = FUN_0066bf30(0, arg; 2, edx, 0, ecx, eax); mark +0x1c bit 0 and push it on the room's +0x38 list."""
            n = m.call("c66bf30", 0, a["stack"][0], 2, a["edx"], 0, a["ecx"], a["regs"]["eax"])
            s = a["regs"]["esi"]
            m.w(n + 0x1C, m.r(n + 0x1C) | 1)
            m.w(n + 0xC, m.r(s + 0x38))
            m.w(s + 0x38, n)
            return n
        rig.run("FUN_00666170", 0x666170, b_link, m_link, 1)

        def b_row(img, rng):
            """A room (ESI) with a size record (+0xc, width at +0x10); x in EAX, y in ECX, pool in EDI."""
            self.node = 0
            put(img, 0x300 + 0xC, at(0x400))
            put(img, 0x400 + 0x10, rng.randrange(0, 90))
            return {"regs": {"eax": rng.randrange(0, 60), "esi": at(0x300), "edi": rng.getrandbits(32)},
                    "ecx": rng.randrange(0, 60)}

        def m_row(m, a):
            """Scale x, y and the end (x + width) by 5, then one node per 0x28 across: FUN_0066bf30(0, pool; 2, 0x41, 0, y * 5, x) pushed on +0x38."""
            s, x, y = a["regs"]["esi"], a["regs"]["eax"] * 5, a["ecx"] * 5
            end = (a["regs"]["eax"] + m.r(m.r(s + 0xC) + 0x10)) * 5
            if x < end:
                while True:
                    n = m.call("c66bf30", 0, a["regs"]["edi"], 2, 0x41, 0, y, x)
                    m.w(n + 0x1C, m.r(n + 0x1C) | 1)
                    m.w(n + 0xC, m.r(s + 0x38))
                    m.w(s + 0x38, n)
                    x += 0x20 if mut("step") else 0x28
                    if not x < end:
                        break
        rig.run("FUN_006661a0", 0x6661A0, b_row, m_row, 0)

        def b_ents(img, rng):
            """A room (EAX) with a list (+0x54 -> +0x14: array, count) of 0..4 entries of 0x30 bytes, flags at +0x14."""
            n = rng.randrange(0, 5)
            put(img, 0x54, at(0x300))
            put(img, 0x300 + 0x14, at(0x500))
            put(img, 0x300 + 0x18, n)
            for i in range(n):
                put(img, 0x500 + 0x30 * i + 0x14, rng.choice((4, 4, 0, 0x4004, 0x1C004, 0x10004, rng.getrandbits(32))))
            return {"regs": {"eax": at(0)}, "ecx": 0, "stack": [rng.getrandbits(32)]}

        def m_ents(m, a):
            """For each entry with bit 2 set and none of 0x1c000: FUN_0067c4f0(ecx = arg, edx = +8; +0xc, 8, 0)."""
            p = m.r(m.r(a["regs"]["eax"] + 0x54) + 0x14)
            for i in range(m.r(m.r(a["regs"]["eax"] + 0x54) + 0x18)):
                e = p + 0x30 * i
                f = m.r(e + 0x14)
                if f & 4 and not f & (0x1C000 if not mut("ent-mask") else 0xC000):
                    m.call("c67c4f0", a["stack"][0], m.r(e + 8), m.r(e + 0xC), 8, 0)
        rig.run("FUN_0066c7f0", 0x66C7F0, b_ents, m_ents, 1)

        def b_add(img, rng):
            """An object (ECX) whose list (+0x54) has a count (+0xc) and an array (+0x1c) of 0x30-byte rows; an optional head slot (EDX)."""
            put(img, 0x54, at(0x300))
            put(img, 0x300 + 0xC, rng.randrange(0, 6))
            put(img, 0x300 + 0x1C, at(0x800))
            slot = rng.choice((0, at(0x700)))
            put(img, 0x700, rng.choice((0, at(0x800 + 0x30))))
            return {"ecx": at(0), "edx": slot, "stack": [rng.getrandbits(32) for _ in range(4)]}

        def m_add(m, a):
            """Row = array + 0x30 * count; +0x20 = old head ([edx]) and [edx] = row, or +0x20 = 0; count + 1; FUN_0066dde0(ecx, row; 4 args); return the row."""
            o = a["ecx"]
            lst = m.r(o + 0x54)
            row = m.r(lst + 0x1C) + 0x30 * m.r(lst + 0xC)
            if a["edx"]:
                m.w(row + 0x20, m.r(a["edx"]))
                m.w(a["edx"], row)
            else:
                m.w(row + 0x20, 0)
            m.w(lst + 0xC, m.r(lst + 0xC) + 1)
            m.call("c66dde0", o, row, *a["stack"])
            return row
        rig.run("FUN_0066def0", 0x66DEF0, b_add, m_add, 4)

    def geometry(self):
        """FUN_0066d7f0, 0067a550, 0067d280, 0067d7e0."""
        rig, at = self.rig, self.at

        rig.run("FUN_0066d7f0", 0x66D7F0, lambda img, rng: {"ecx": rng.randrange(0, 16)},
                lambda m, a: self.word(0x6E_EF20 + 4 * a["ecx"]), 0)

        def b_pk(img, rng):
            """Two packed points of random 16-bit parts."""
            return {"stack": [rng.getrandbits(32), rng.getrandbits(32)]}

        def m_oct(m, a):
            """dx, dy = absolute part differences; the smaller + twice the larger."""
            p, q = a["stack"]
            dx, dy = abs((q & 0xFFFF) - (p & 0xFFFF)), abs((q >> 16) - (p >> 16))
            lo, hi = min(dx, dy), max(dx, dy)
            return lo * 2 + hi if mut("oct") else lo + 2 * hi
        rig.run("FUN_0067a550", 0x67A550, b_pk, m_oct, 2)

        def b_sign(img, rng):
            """A point (ECX) whose +0x10 is another; outputs at 0x500 / 0x504 (EDX and the stack)."""
            put(img, 0x10, at(0x300))
            for off in (0, 4):
                put(img, off, rng.choice((0, 5, rng.getrandbits(32), -3 & M32)))
                put(img, 0x300 + off, rng.choice((0, 5, rng.getrandbits(32), 0x7FFFFFF0)))
            return {"ecx": at(0), "edx": at(0x500), "stack": [at(0x504)]}

        def m_sign(m, a):
            """Signs (-1, 0, 1) of the dword differences other - this, x to EDX and y to the stack pointer."""
            o = a["ecx"]
            t = m.r(o + 0x10)
            for k, out in ((0, a["edx"]), (4, a["stack"][0])):
                d = s32(m.r(t + k) - m.r(o + k))
                m.w(out, (-1 if d < 0 else (1 if d > 0 or (mut("sign") and d == 0) else 0)) & M32)
        rig.run("FUN_0067d280", 0x67D280, b_sign, m_sign, 1)

        def b_side(img, rng):
            """An out record (ECX), a side (EDX) of 0, 1 or other, a source point (ESI) and a step index on the stack."""
            put(img, 0x500, rng.getrandbits(32))
            put(img, 0x504, rng.getrandbits(32))
            return {"ecx": at(0x300), "edx": rng.choice((0, 1, 2, 3)), "regs": {"esi": at(0x500)},
                    "stack": [rng.randrange(0, 4)]}

        def m_side(m, a):
            """Side 0 sets +8, +0xc to 0x40, 0xa0, side 1 to 0xa0, 0x40; then +0 and +4 = the step table (index side + 2 * step) plus the source point."""
            o, side, src = a["ecx"], a["edx"], a["regs"]["esi"]
            if side == 0:
                m.w(o + 8, 0x40)
                m.w(o + 0xC, 0xA0)
            elif side == 1:
                m.w(o + 8, 0xA0)
                m.w(o + 0xC, 0x40)
            i = side + 2 * a["stack"][0]
            m.w(o, self.word(0x6F_1F7C + 8 * i) + m.r(src))
            m.w(o + 4, self.word(0x6F_1F80 + 8 * i) + m.r(src + 4))
        rig.run("FUN_0067d7e0", 0x67D7E0, b_side, m_side, 1)

    def seed(self):
        """FUN_0067a630 on the emulator's own memory (the object is 0x37000 bytes)."""
        rig, e = self.rig, self.rig.m
        obj = e.alloc(0x37100)
        seed, st, cell = obj + 0x37000, obj + 0x37040, obj + 0x37080
        for _ in range(rig_mod.ROUNDS * 4):
            rng = rig.rng
            on = rng.choice((0, 1, 1, 1))
            lo, hi = rng.getrandbits(32), rng.getrandbits(32)
            e.mu.mem_write(obj + 0x36FD0, struct.pack("<II", on, seed))
            e.mu.mem_write(seed, struct.pack("<II", lo, hi))
            dword, cur = rng.getrandbits(32), rng.getrandbits(32)
            e.mu.mem_write(st + 0xC, struct.pack("<I", cell))
            e.mu.mem_write(cell, struct.pack("<I", dword))
            e.mu.mem_write(st + 0x10, struct.pack("<I", cur))
            e.call(0x67A630, regs={"eax": obj, "esi": st}, limit=100000)
            got = struct.unpack("<I", e.read(st + 0x10, 4))[0]
            gseed = struct.unpack("<II", e.read(seed, 8))
            if on:
                prod = lo * 0x6AC6_90C5 + hi
                want_seed = (prod & M32, (prod >> 32) & M32)
                want = (self.word(0x6F_18D8 + 4 * (prod & (0xF if mut("seed-mask") else 0x1F))) + dword + cur) & 7
            else:
                want_seed, want = (lo, hi), (dword + cur) & 7
            rig_mod.require(got == want and gseed == want_seed,
                            f"FUN_0067a630 on={on}: dir {got}, model {want}; seed {gseed}, model {want_seed}")
            rig.cases += 1

    def moves(self):
        """FUN_00679f70, 0067a390, 0067a420 (with FUN_00643560 and FUN_00678f00 for real where called)."""
        rig, at = self.rig, self.at

        def b_pos(img, rng):
            """An object (ECX) with a position (two words), a target (+4) and a record (+0x30); the new position FUN_00679a60 gives."""
            put(img, 4, rng.getrandbits(32))
            put(img, 0x30, at(0x300))
            put(img, 0, rng.getrandbits(32))
            self.new = rng.getrandbits(32)
            if rng.random() < 0.5:
                self.new = int.from_bytes(img[0:4], "little")
            elif rng.random() < 0.5:
                self.new = (int.from_bytes(img[0:2], "little")) | (rng.getrandbits(16) << 16)
            return {"ecx": at(0)}

        def m_pos(m, a):
            """Clear +0x24, save +4 at +0x9c, ask FUN_00679a60; if the new position equals the object's x, y return 0, else store it at +0x9c, set +0x28 and return 1."""
            o = a["ecx"]
            s = m.r(o + 0x30)
            m.w(s + 0x24, 0)
            m.w(s + 0x9C, m.r(o + 4))
            v = m.call("c679a60", s)
            if m.r(o, 2) == v & 0xFFFF and (mut("word") or m.r(o + 2, 2) == v >> 16):
                return 0
            m.w(s + 0x9C, v)
            m.w(s + 0x28, 1)
            return 1
        rig.run("FUN_00679f70", 0x679F70, b_pos, m_pos, 0)

        def vec(rng):
            """A direction vector: small, large or zero parts."""
            return rng.choice((0, 1, -1, rng.randrange(-3000, 3000), rng.randrange(-1 << 20, 1 << 20), -0x8000_0000)) & M32

        def b_step(img, rng):
            """Scale in ECX, output at 0x300 (EDX), two stack words; the vector FUN_0064fd70 will write."""
            self.vec = (vec(rng), vec(rng))
            return {"ecx": rng.choice((0, 0x100, 0x180, 0x200, 3, rng.randrange(0, 0x3000))), "edx": at(0x300),
                    "stack": [rng.getrandbits(32), rng.getrandbits(32)]}

        def m_step(m, a):
            """FUN_0064fd70 fills the vector; scale it (x * scale >> 8), store, return the larger absolute part; fatal 0x331 from 0x200000."""
            m.call("c64fd70", *a["stack"])
            x, y, big = scaled(*self.vec, a["ecx"])
            m.w(a["edx"], x)
            m.w(a["edx"] + 4, y)
            if big >= 0x20_0000:
                raise Halt(0x331)
            return big
        rig.run("FUN_0067a390", 0x67A390, b_step, m_step, 2)

        def b_move(img, rng):
            """An object (EDI) with a scale (+0x7c, may be 0); the same vector and arguments as above."""
            put(img, 0x7C, rng.choice((0, 0x100, 0x180, 0x200, 3, 1, rng.randrange(0, 0x3000))))
            self.vec = (vec(rng), vec(rng))
            if rng.random() < 0.2:
                self.vec = (rng.choice((0, 1)), 0)
            return {"regs": {"edi": at(0)}, "stack": [rng.getrandbits(32), rng.getrandbits(32)]}

        def m_move(m, a):
            """Without a scale return 0; else vector = FUN_0067a390, fatal 0x342 when it is the zero vector, FUN_00678f00 clamps it, FUN_00679530 gives the result."""
            o = a["regs"]["edi"]
            scale = m.r(o + 0x7C)
            if scale == 0:
                return 0
            m.call("c64fd70", *a["stack"])
            x, y, big = scaled(*self.vec, scale)
            if big >= 0x20_0000:
                raise Halt(0x331)
            if not (abs32(x) > 0 or abs32(y) > 0):
                raise Halt(0x342)
            x, y = clamp(x, y)
            got = m.call("c679530", o, a["stack"][0])
            rig_mod.require(got == mix("c679530", o, x, y, a["stack"][0], a["stack"][1]),
                            f"FUN_0067a420: FUN_00679530 got another vector than the model's ({x}, {y})")
            return got
        rig.run("FUN_0067a420", 0x67A420, b_move, m_move, 2)


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 80
    s = Suite(rig)
    s.lists()
    s.geometry()
    s.seed()
    s.moves()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("oct", "sign", "step", "ent-mask", "seed-mask", "word", "shift", "clamp")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "DRLG geometry, direction and list-link helper", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
