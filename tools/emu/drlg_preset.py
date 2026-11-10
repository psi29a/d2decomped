# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify small Preset.cpp and RoomTile.cpp helpers of DRLG in stock game.exe 1.14d.

    uv run python drlg_preset.py [--mutation-check]

Runs game.exe's own code for FUN_00666610 and FUN_00666630 (per-room preset
block), FUN_00666a80, FUN_00666fd0, FUN_00666fe0, FUN_00667010, FUN_00667030
(preset record accessors), FUN_006675d0 (free a preset map), FUN_0066e140
(a constant by kind), FUN_0066e5e0 (push a list node), FUN_0066dc10 (a point
in room tiles), FUN_0066efb0 (grow a tile array), FUN_0066f050 (free a tile
grid's arrays), FUN_0066f350 and FUN_0066f380 (a room's grid flags) against
hand-written models on random records (_dungeon_rig.py): return value, callee
log, arena, allocator blocks and the bytes `ret n` pops. Several take the
room in EAX, ESI or EDI. The CRT memset runs for real, with an allocator that
returns dirty memory. Hooked, logged, not claimed: FUN_00666520, FUN_0061f0b0,
FUN_00643310, FUN_0067c570, the allocator and realloc. `--mutation-check`
breaks the model three ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, s32  # noqa: F401

MUT = set()
F_PRESET = 0x6E_E7C4
F_TILE = 0x6E_F540


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
        rig.callee("c666520", 0x66_6520, 0, ("eax", "ecx", "edx", "edi"))
        rig.callee("c61f0b0", 0x61_F0B0, 1, ())
        rig.callee("c643310", 0x64_3310, 2, (), nlog=0)
        rig.callee("c67c570", 0x67_C570, 1, ("ecx", "edx"))
        rig.callee("realloc", 0x40_B4B0, 4, ("ecx", "edx"))
        rig.custom["c61f0b0"] = lambda e: self.base + 0x900

        def tweak(e):
            """FUN_00643310 stand-in: *arg0 += 7, *arg1 ^= 5."""
            a, b = e.arg(0), e.arg(1)
            e.w32(a, (e.r32(a) + 7) & M32)
            e.w32(b, e.r32(b) ^ 5)
            return 0
        rig.custom["c643310"] = tweak

        def dirty(e):
            """Allocator stand-in returning dirty memory, so memsets are visible."""
            from unicorn.x86_const import UC_X86_REG_EDX
            size = max(e.mu.reg_read(UC_X86_REG_EDX), 1)
            p = e.alloc(size)
            e.mu.mem_write(p, b"\xAA" * size)
            return p
        rig.custom["alloc"] = dirty
        rig.custom["realloc"] = lambda e: e.alloc(max(e.arg(0), 1))

    def at(self, off):
        """Arena address of offset `off`."""
        return self.base + off

    def preset(self):
        """FUN_00666610, 00666630, 00666a80, 00666fd0, 00666fe0, 00667010, 00667030, 006675d0."""
        rig, at = self.rig, self.at

        def b_room(img, rng):
            """A room (0) whose level (+0x58) is at 0x300, its act (+0x1b4) at 0x600, a preset block at +0x20."""
            put(img, 0x58, at(0x300))
            put(img, 0x300 + 0x1B4, at(0x600))
            put(img, 0x20, rng.choice((0, at(0x1000))))
            put(img, 0x48, rng.choice((1, 2, 2)))
            return {"ecx": at(0)}

        def m_walk(m, a):
            """When +0x20 is set: FUN_00666520(room in EAX, level, act, pool in EDI)."""
            r = a["ecx"]
            if m.r(r + 0x20):
                lv = m.r(r + 0x58)
                act = m.r(lv + 0x1B4)
                m.call("c666520", r, lv, act, m.r(act + 0x478))
        rig.run("FUN_00666610", 0x666610, b_room, m_walk, 0)

        def m_block(m, a):
            """Allocate 0xf8 from the act's pool (line 0x70f), zero it, store at +0x20."""
            r = a["ecx"]
            p = m.alloc(m.r(m.r(m.r(r + 0x58) + 0x1B4) + 0x478), 0xF8, 0x70F, F_PRESET)
            m.w(r + 0x20, p)
        rig.run("FUN_00666630", 0x666630, b_room, m_block, 0)

        def b_pair(img, rng):
            """A room with a preset block at 0x1000 holding two words at +0xf0 / +0xf4."""
            put(img, 0x48, rng.choice((1, 2, 2)))
            put(img, 0x20, at(0x1000))
            return {"ecx": at(0), "edx": at(0x200), "stack": [at(0x210)]}

        def m_pair(m, a):
            """Preset type 2: out1 = +0xf0, out2 = +0xf4 of the block; else both 0."""
            r = a["ecx"]
            if m.r(r + 0x48) == 2:
                p = m.r(r + 0x20)
                m.w(a["edx"], m.r(p + 0xF0))
                m.w(a["stack"][0], m.r(p + 0xF4))
            else:
                m.w(a["edx"], 0)
                m.w(a["stack"][0], 0)
        rig.run("FUN_00666a80", 0x666A80, b_pair, m_pair, 1)

        rig.run("FUN_00666fd0", 0x666FD0, lambda img, rng: {}, lambda m, a: m.r(m.call("c61f0b0", 0) + 0x28), 0)
        rig.run("FUN_00666fe0", 0x666FE0, lambda img, rng: {}, lambda m, a: m.r(m.call("c61f0b0", 0) + 0x2C), 0)

        def b_rec(img, rng):
            """A room -> block (+0x20) -> map (+8) {.., index +4, table +8}."""
            put(img, 0x20, at(0x1000))
            put(img, 0x1000 + 8, at(0x1200))
            put(img, 0x1200 + 4, rng.randrange(0, 5))
            put(img, 0x1200 + 8, at(0x1400))
            return {"ecx": at(0)}
        rig.run("FUN_00667010", 0x667010, b_rec,
                lambda m, a: m.r(m.r(m.r(a["ecx"] + 0x20) + 8) + 8) + 60 * m.r(m.r(m.r(a["ecx"] + 0x20) + 8) + 4) + 0x44, 0)

        def b_two(img, rng):
            """Either output may be null."""
            put(img, 0x20, at(0x1000))
            put(img, 0x1000 + 8, at(0x1200))
            return {"ecx": at(0), "edx": rng.choice((0, at(0x200))), "stack": [rng.choice((0, at(0x210)))]}

        def m_two(m, a):
            """out1 = map[0], out2 = map[1], each if non-null."""
            mp = m.r(m.r(a["ecx"] + 0x20) + 8)
            if a["edx"]:
                m.w(a["edx"], m.r(mp))
            if a["stack"][0]:
                m.w(a["stack"][0], m.r(mp + 4 + (4 if mut("two-slot") else 0)))
        rig.run("FUN_00667030", 0x667030, b_two, m_two, 1)

        def b_free(img, rng):
            """A map with or without a sub-block (+0x10) holding a pointer at +4."""
            put(img, 0x10, rng.choice((0, at(0x300))))
            return {"ecx": rng.getrandbits(32), "edx": at(0)}

        def m_free(m, a):
            """Free the sub-block's +4 (line 0x597) and the sub-block (0x598), then the map (0x56b)."""
            p = a["edx"]
            sub = m.r(p + 0x10)
            if sub:
                m.free(a["ecx"], m.r(sub + 4), 0x597, F_PRESET)
                m.free(a["ecx"], sub, 0x598, F_PRESET)
            m.free(a["ecx"], p, 0x56B, F_PRESET)
        rig.run("FUN_006675d0", 0x6675D0, b_free, m_free, 0)

    def tiles(self):
        """FUN_0066e140, 0066e5e0, 0066dc10, 0066efb0, 0066f050, 0066f350, 0066f380."""
        rig, at = self.rig, self.at

        rig.run("FUN_0066e140", 0x66E140, lambda img, rng: {"stack": [rng.choice((0xB, 0xB, 0, 5, rng.getrandbits(32)))]},
                lambda m, a: 0x72 if a["stack"][0] == 0xB else 0x6C, 1, mask=0xFF)

        def b_push(img, rng):
            """A room (ESI) whose +0x54 points at a list head cell."""
            put(img, 0x54, at(0x300))
            put(img, 0x300, rng.choice((0, at(0x500))))
            return {"ecx": rng.getrandbits(32), "regs": {"esi": at(0)}, "stack": [rng.getrandbits(32)]}

        def m_push(m, a):
            """Allocate 12 (line 0x37b), node = {value, 0, old head}, head = node."""
            r = a["regs"]["esi"]
            cell = m.r(r + 0x54)
            p = m.alloc(a["ecx"], 12, 0x37B, F_TILE)
            m.w(p, a["stack"][0])
            m.w(p + 4, 0)
            m.w(p + 8, m.r(cell))
            m.w(cell, p)
        rig.run("FUN_0066e5e0", 0x66E5E0, b_push, m_push, 1)

        def b_pt(img, rng):
            """An output record (ESI), a room (EDX) with a tile origin, a point."""
            put(img, 0x200 + 0x34, rng.randrange(0, 5))
            put(img, 0x200 + 0x38, rng.randrange(0, 5))
            return {"regs": {"esi": at(0)}, "edx": at(0x200), "stack": [rng.randrange(0, 20), rng.randrange(0, 20)]}

        def m_pt(m, a):
            """[esi+8] = x - ox, [esi+0xc] = y - oy; FUN_00643310(&x, &y + 1) adjusts both; [esi] = x', [esi+4] = y' + 0x28."""
            s, room = a["regs"]["esi"], a["edx"]
            x, y = a["stack"]
            m.w(s + 8, x - m.r(room + 0x34))
            m.w(s + 0xC, y - m.r(room + 0x38))
            m.call("c643310")
            x2, y2 = (x + 7) & M32, ((y + 1) ^ 5) & M32
            m.w(s, x2)
            m.w(s + 4, y2 + 0x28)
        rig.run("FUN_0066dc10", 0x66DC10, b_pt, m_pt, 2)

        def b_grow(img, rng):
            """A tile array record (EDX): +0x10 entries, +0x24 array, +0x28 count; ECX is the pool."""
            cnt = rng.randrange(0, 4)
            n = rng.choice((0, 1, 2, 3))
            put(img, 0x10, rng.randrange(0, cnt + n + 1))
            put(img, 0x24, at(0x1000))
            put(img, 0x28, cnt)
            return {"ecx": rng.getrandbits(32), "edx": at(0), "stack": [n]}

        def m_grow(m, a):
            """With n > 0: realloc the array to (count + n) * 0x30 (line 0x561), chain the first entries' +0x20, null-end the next one, count += n."""
            t, n = a["edx"], a["stack"][0]
            if n == 0:
                return
            old, cnt = m.r(t + 0x24), m.r(t + 0x28)
            p = m.realloc(a["ecx"], old, (cnt + n) * 0x30, 0x561, F_TILE)
            m.w(t + 0x24, p)
            k = 0
            while k < s32(m.r(t + 0x10)) - 1:
                m.w(p + 0x30 * k + 0x20, p + 0x30 * k + 0x30)
                k += 1
            m.w(p + 0x30 * k + 0x20, 0)
            m.w(t + 0x28, cnt + n + (1 if mut("grow-count") else 0))
        rig.run("FUN_0066efb0", 0x66EFB0, b_grow, m_grow, 1)

        def b_tfree(img, rng):
            """A room (EAX) with a tile grid (+0x54) whose three arrays at +0x14/+0x1c/+0x24 may be null; EDI pool."""
            put(img, 0x54, rng.choice((0, at(0x300))))
            for o in (0x14, 0x1C, 0x24):
                put(img, 0x300 + o, rng.choice((0, at(0x500 + o))))
            return {"regs": {"eax": at(0), "edi": rng.getrandbits(32)}}

        def m_tfree(m, a):
            """Free the three arrays that are set (lines 0x592, 0x593, 0x594); the pointers are left in place."""
            g = m.r(a["regs"]["eax"] + 0x54)
            if g:
                for off, line in ((0x14, 0x592), (0x1C, 0x593), (0x24, 0x594)):
                    if m.r(g + off):
                        m.free(a["regs"]["edi"], m.r(g + off), line, F_TILE)
        rig.run("FUN_0066f050", 0x66F050, b_tfree, m_tfree, 0)

        def b_gf(img, rng):
            """A room (EAX) whose +0x50 record has flags at +0x18 and +0x14; x in EDX, y on the stack."""
            put(img, 0x50, at(0x300))
            put(img, 0x300 + 0x18, rng.choice((0, 1)))
            put(img, 0x300 + 0x14, rng.choice((0, 1)))
            return {"regs": {"eax": at(0)}, "edx": rng.getrandbits(32), "stack": [rng.getrandbits(32)]}

        def m_gf(flag, grid):
            """When the record's flag is set: FUN_0067c570(grid, y, x) (arguments swapped); else 0."""
            def model(m, a):
                """The model for one of the two."""
                r = a["regs"]["eax"]
                if m.r(m.r(r + 0x50) + flag) == 0:
                    return 0
                return m.call("c67c570", r + grid, a["stack"][0], a["edx"] if not mut("grid-swap") else a["stack"][0])
            return model
        rig.run("FUN_0066f350", 0x66F350, b_gf, m_gf(0x18, 0xF4), 1)
        rig.run("FUN_0066f380", 0x66F380, b_gf, m_gf(0x14, 0xA4), 1)


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 80
    s = Suite(rig)
    s.preset()
    s.tiles()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("two-slot", "grow-count", "grid-swap")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "Preset.cpp and RoomTile.cpp helper", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
