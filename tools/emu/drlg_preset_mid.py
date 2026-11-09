# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the Preset.cpp file cache and POP tile updaters in stock game.exe 1.14d.

    uv run python drlg_preset_mid.py [--mutation-check]

Runs game.exe's own code for FUN_00665f40, FUN_00666e00, FUN_00667060,
FUN_00667430, FUN_00668240 and FUN_00668300 against hand-written models on
random records (_dungeon_rig.py): return value, callee log, arena, allocator
blocks, fatal-error line, the cache's global head (0x96d60c) and the bytes
`ret n` pops. The CRT memset runs for real, with an allocator that returns
dirty memory for the file record. The import stubs give InterlockedIncrement
and Decrement their real effect. Hooked, logged, not claimed: FUN_00665950,
FUN_0066b980, FUN_00604b90, FUN_0061f0e0, FUN_0045c3e0, FUN_00642d10,
FUN_0040b3c0 and the allocator. `--mutation-check` breaks the model five ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt, require, s32

MUT = set()
F_PRESET = 0x6E_E7C4
CACHE_HEAD = 0x96_D60C


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def put(img, off, value):
    """Write a dword at arena offset `off`."""
    img[off:off + 4] = (value & M32).to_bytes(4, "little")


def tdiv(a, b):
    """C division truncating toward zero (idiv)."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig):
        """Hook the callees this oracle leaves unclaimed."""
        from unicorn.x86_const import UC_X86_REG_EDX
        self.rig = rig
        self.base = rig.arena
        rig.callee("c665950", 0x66_5950, 1, ("ecx", "edx"))
        rig.callee("c66b980", 0x66_B980, 1, ("edx",))
        rig.callee("c604b90", 0x60_4B90, 1, ())
        self.exp_rects = []

        def inside(e):
            """FUN_0066b980 stand-in: note the rectangle (ECX points at four dwords on the caller's stack), answer a random bit."""
            from unicorn.x86_const import UC_X86_REG_ECX
            ptr = e.mu.reg_read(UC_X86_REG_ECX)
            self.rects.append(tuple(e.r32(ptr + 4 * k) for k in range(4)))
            return rig.rng.getrandbits(1)
        rig.custom["c66b980"] = inside
        rig.custom["c604b90"] = lambda e: rig.rng.randrange(0, 4)
        self.rects = []

        def dirty_file(e):
            """Allocator stand-in: dirty memory for the 0x5c file record (so its memset is visible), clean for the rest."""
            size = max(e.mu.reg_read(UC_X86_REG_EDX), 1)
            p = e.alloc(size)
            if size == 0x5C:
                e.mu.mem_write(p, b"\xAA" * size)
            return p
        rig.custom["alloc"] = dirty_file

    def at(self, off):
        """Arena address of offset `off`."""
        return self.base + off

    def inside(self, m, rect, x, y):
        """Log a FUN_0066b980 call in the model, remembering the rectangle it should have been given."""
        self.exp_rects.append(tuple(v & M32 for v in rect))
        return m.call("c66b980", x, y)

    def checked(self, build, model):
        """Wrap a case builder and model so the rectangles the inside-test hook saw are compared too."""
        def b(img, rng):
            """Clear the rectangle notes, then build."""
            self.rects.clear()
            self.exp_rects.clear()
            return build(img, rng)

        def m(mm, a):
            """Run the model, then compare the rectangles."""
            out = model(mm, a)
            require(self.rects == self.exp_rects, f"rectangles {self.rects}, model {self.exp_rects}")
            return out
        return b, m

    def cache(self):
        """FUN_00665f40: the DS1 file cache (find by name, count a reference, or allocate and parse)."""
        rig, at = self.rig, self.at

        def build(img, rng):
            """A cache list of nodes with short names and a wanted name; the global head points at the first node."""
            names = ["", "a", "ab", "b", "ba", "abc", "aab"]
            put(img, 0x40, 0)
            want = rng.choice(names[1:])
            img[0x800:0x800 + len(want) + 1] = want.encode() + b"\0"
            head = 0
            for k in range(rng.randrange(0, 4)):
                node = 0x1000 + 0x120 * k
                nm = rng.choice(names)
                img[node:node + len(nm) + 1] = nm.encode() + b"\0"
                put(img, node + 0x104, rng.randrange(1, 9))
                put(img, node + 0x108, rng.getrandbits(32))
                put(img, node + 0x10C, 0)
                if head:
                    put(img, 0x1000 + 0x120 * (k - 1) + 0x10C, at(node))
                else:
                    head = at(node)
            rig.m.mu.mem_write(CACHE_HEAD, head.to_bytes(4, "little"))
            return {"ecx": at(0x40), "edx": rng.getrandbits(32), "stack": [at(0x800)]}

        def model(m, a):
            """Look the name up; a hit counts a reference and returns that file; a miss allocates, copies the name, links at the head and parses."""
            out, pool, name = a["ecx"], a["edx"], a["stack"][0]
            head = int.from_bytes(rig.m.read(CACHE_HEAD, 4), "little")      # the global is outside the arena: read after the stock run
            want = bytearray()
            while m.r(name + len(want), 1):
                want.append(m.r(name + len(want), 1))
            # the head before the run: rebuild it from the case (the stock run changed it on a miss)
            first = self.first_node
            node = first
            while node:
                nm = bytearray()
                while m.r(node + len(nm), 1):
                    nm.append(m.r(node + len(nm), 1))
                if nm == want:
                    m.w(node + 0x104, m.r(node + 0x104) + (1 if not mut("no-ref") else 0))
                    m.w(out, m.r(node + 0x108))
                    require(head == first, f"cache head moved on a hit: {head:#x}")
                    return None
                node = m.r(node + 0x10C)
            node = m.alloc(0, 0x110, 0x4E0, F_PRESET)
            fil = m.alloc(0, 0x5C, 0x4E1, F_PRESET)
            m.w(out, fil)
            for k, ch in enumerate(want + b"\0"):
                m.w(node + k, ch, 1)
            m.w(node + 0x104, 1)
            m.w(node + 0x108, fil)
            m.w(node + 0x10C, first)
            require(head == node, f"cache head {head:#x}, model {node:#x}")
            m.call("c665950", fil, pool, name)
            return None

        def build_and_note(img, rng):
            """Build, and remember the head the case started with."""
            args = build(img, rng)
            self.first_node = int.from_bytes(rig.m.read(CACHE_HEAD, 4), "little")
            return args
        rig.run("FUN_00665f40", 0x665F40, build_and_note, model, 1, ret=False)

    def tiles(self):
        """FUN_00667060: refresh the POP tiles of a room's wall array inside a rectangle."""
        rig, at = self.rig, self.at

        def build(img, rng):
            """A room with a tile grid of wall tiles of random flags, and a coords rectangle."""
            put(img, 0x100 + 0x54, 0 if rng.random() < 0.1 else at(0x200))
            put(img, 0x100 + 0x34, rng.randrange(0, 20))
            put(img, 0x100 + 0x38, rng.randrange(0, 20))
            put(img, 0x200 + 0x14, at(0x400))
            put(img, 0x200 + 0x18, rng.choice((0, 1, 3, 4)))
            for k in range(4):
                t = 0x400 + 0x30 * k
                put(img, t + 0x14, rng.choice((0, 0x200, 0x100, 0x300, 0x208, 0x8)) | rng.choice((0, 0x7)))
                put(img, t + 0x18, rng.getrandbits(32))
                put(img, t + 0x24, rng.getrandbits(32) & ~6)
                put(img, t + 0x28, rng.choice((0, 255, 1, 100, 254, rng.randrange(0, 256))) | rng.getrandbits(24) << 8)
                put(img, t + 0xC, rng.randrange(0, 20))
                put(img, t + 0x8, rng.randrange(0, 20))
            for k in range(4):
                put(img, 0x300 + 4 * k, rng.randrange(0, 10))
            return {"regs": {"ebx": at(0x100), "edi": rng.randrange(0, 1 << 20)}, "ecx": at(0x300),
                    "stack": [rng.randrange(0, 4), rng.choice((0, 1))]}

        def model(m, a):
            """Mark the tiles inside the padded rectangle that are POP tiles (0x200) or whose library index matches; set or clear their fade."""
            room, deadline = a["regs"]["ebx"], a["regs"]["edi"]
            want, flag = a["stack"]
            coords = a["ecx"]
            tg = m.r(room + 0x54)
            if tg == 0:
                raise Halt(0xA3B)
            x, y, w, h = m.r(coords), m.r(coords + 4), m.r(coords + 8), m.r(coords + 12)
            rect = (x - 1, y - 1, w + 2, h + 2)
            n, arr = s32(m.r(tg + 0x18)), m.r(tg + 0x14)
            for k in range(max(n, 0)):
                t = arr + 0x30 * k
                tx = m.r(t + 8) + m.r(room + 0x34)
                ty = m.r(t + 0xC) + m.r(room + 0x38)
                if m.r(t + 0x14) & 0x200:
                    if self.inside(m, rect, tx, ty):
                        if flag:
                            m.w(t + 0x24, m.r(t + 0x24) | 4)
                            m.w(t + 0x2C, deadline)
                        else:
                            m.w(t + 0x24, m.r(t + 0x24) & ~4)
                            m.w(t + 0x14, m.r(t + 0x14) & ~(8 if not mut("keep-8") else 0))
                        continue
                if m.r(t + 0x14) & 0x100:
                    continue
                if m.call("c604b90", m.r(t + 0x18)) != want:
                    continue
                if not self.inside(m, rect, tx, ty):
                    continue
                m.w(t + 0x24, m.r(t + 0x24) | 2)
                al = m.r(t + 0x28, 1)
                if flag:
                    m.w(t + 0x29, 0, 1)
                    m.w(t + 0x2A, 0xFF, 1)
                    m.w(t + 0x2C, deadline if al == 0 else (deadline - tdiv(al * 500, 255)) & M32)
                else:
                    m.w(t + 0x29, 0xFF, 1)
                    m.w(t + 0x2A, 0, 1)
                    m.w(t + 0x2C, deadline if al == 0xFF else (deadline + tdiv((al - 255) * 500, 255 + (1 if mut("fade") else 0))) & M32)
        rig.run("FUN_00667060", 0x667060, *self.checked(build, model), 2, ret=False)


def more_methods():
    """The file release, level init and POP location groups, added to Suite below."""
    def free_model(m, fil):
        """DRLGPRESET_FreeDrlgFile's effects on a model: the DS1 data, group array, preset units (and their map AI) and the record."""
        if m.r(fil + 4):
            m.call("cfree", m.r(fil + 4), F_PRESET, 0x4F8, 0)
        if m.r(fil + 0x50):
            m.free(0, m.r(fil + 0x50), 0x4F9, F_PRESET)
        unit = m.r(fil + 0x54)
        while unit:
            ai, nxt = m.r(unit + 0x10), m.r(unit + 0xC)
            if ai:
                m.free(0, m.r(ai + 4), 0x597 if not mut("ai-line") else 0x598, F_PRESET)
                m.free(0, ai, 0x598, F_PRESET)
            m.free(0, unit, 0x56B, F_PRESET)
            unit = nxt
        m.free(0, fil, 0x505, F_PRESET)

    def file_build(img, rng, at):
        """A file record at 0x40 + 0x80 k with a DS1 pointer, a group array and a chain of preset units (some with map AI)."""
        def rec(base, units_at):
            """Fill one file record at arena offset `base`."""
            put(img, base + 4, rng.choice((0, rng.getrandbits(32) | 1)))
            put(img, base + 0x50, rng.choice((0, rng.getrandbits(32) | 1)))
            first = 0
            prev = 0
            for k in range(rng.randrange(0, 4)):
                u = units_at + 0x40 * k
                put(img, u + 0xC, 0)
                ai = rng.random() < 0.5
                put(img, u + 0x10, at(u + 0x20) if ai else 0)
                put(img, u + 0x24, rng.getrandbits(32) | 1)
                if prev:
                    put(img, prev + 0xC, at(u))
                else:
                    first = at(u)
                prev = u
            put(img, base + 0x54, first)
        return rec

    def free_file(self):
        """FUN_00668240: free a DS1 file record and everything it owns."""
        rig, at = self.rig, self.at
        rig.callee("cfree", 0x40_B3C0, 2, ("ecx", "edx"))

        def build(img, rng):
            """A file record at 0x100 with its units from 0x400."""
            file_build(img, rng, at)(0x100, 0x400)
            return {"ecx": at(0x100)}

        def model(m, a):
            """Free in the order: DS1 data (client free), group array, each unit's AI position + AI + unit, the record."""
            free_model(m, a["ecx"])
        rig.run("FUN_00668240", 0x668240, build, model, 0, ret=False)

    def release(self):
        """FUN_00668300: drop a reference on a cached DS1 file; at zero unlink, free it and its node."""
        rig, at = self.rig, self.at

        def build(img, rng):
            """A cache of 1-3 nodes whose files are records at 0x1000 + 0x400 k; the wanted file is one of them."""
            nodes = rng.randrange(1, 4)
            for k in range(nodes):
                node = 0x100 + 0x120 * k
                fil = 0x1000 + 0x400 * k
                put(img, node + 0x104, rng.choice((1, 1, 2, 0, -1)))
                put(img, node + 0x108, at(fil))
                put(img, node + 0x10C, at(0x100 + 0x120 * (k + 1)) if k + 1 < nodes else 0)
                file_build(img, rng, at)(fil, fil + 0x100)
            head = at(0x100)
            rig.m.mu.mem_write(CACHE_HEAD, head.to_bytes(4, "little"))
            self.first_node = head
            pick = rng.randrange(nodes)
            put(img, 0x40, 0 if rng.random() < 0.1 else at(0x1000 + 0x400 * pick))
            return {"ecx": at(0x40)}

        def model(m, a):
            """Null: nothing. Else find the node, decrement; at <= 0 unlink, free the file and node, null the pointer."""
            pp = a["ecx"]
            target = m.r(pp)
            head = int.from_bytes(rig.m.read(CACHE_HEAD, 4), "little")
            if target == 0:
                require(head == self.first_node, "cache head moved on a null release")
                return
            prev, node = 0, self.first_node
            while node and m.r(node + 0x108) != target:
                prev, node = node, m.r(node + 0x10C)
            ref = (m.r(node + 0x104) - 1) & M32
            m.w(node + 0x104, ref)
            if s32(ref) > 0:
                require(head == self.first_node, "cache head moved without a free")
                return
            if prev:
                m.w(prev + 0x10C, m.r(node + 0x10C))
                require(head == self.first_node, "cache head moved on a mid-list unlink")
            else:
                require(head == m.r(node + 0x10C), f"cache head {head:#x}, model {m.r(node + 0x10C):#x}")
            free_model(m, target)
            m.free(0, node, 0x538, F_PRESET)
            m.w(pp, 0)
        rig.run("FUN_00668300", 0x668300, build, model, 0, ret=False)

    def init_area(self):
        """FUN_00667430: allocate a preset level record, pick a file and size the level."""
        rig, at = self.rig, self.at
        rig.callee("c61f0e0", 0x61_F0E0, 0, ("ecx",))
        rig.callee("c45c3e0", 0x45_C3E0, 0, ("ecx", "edx"))
        rig.callee("c642d10", 0x64_2D10, 0, ("ecx", "edx"))
        self.prest = 0
        rig.custom["c61f0e0"] = lambda e: self.prest

        def build(img, rng):
            """A level with an act pool, a level id and a LvlPrest record (or none) with a file count."""
            put(img, 0x1B4, at(0x300))
            put(img, 0x300 + 0x478, rng.getrandbits(32))
            put(img, 0x1D0, rng.randrange(1, 100))
            self.prest = 0 if rng.random() < 0.15 else at(0x500)
            put(img, 0x500 + 0x40, rng.choice((0, 1, 4, 9)))
            return {"ecx": at(0)}

        def model(m, a):
            """Allocate 8 bytes (line 0xaf9); no LvlPrest row halts at 0xafb; else store the record, pick a file (or -1) and tail-call the sizing."""
            lv = a["ecx"]
            blk = m.alloc(m.r(m.r(lv + 0x1B4) + 0x478), 8, 0xAF9, F_PRESET)
            m.call("c61f0e0", m.r(lv + 0x1D0))
            if self.prest == 0:
                raise Halt(0xAFB)
            m.w(lv + 0x14, blk)
            files = m.r(self.prest + 0x40)
            if files:
                m.w(blk + 4, m.call("c45c3e0", lv + 0x1C4, files))
            else:
                m.w(blk + 4, M32 if not mut("no-file") else 0)
            return m.call("c642d10", m.r(lv + 0x1B4), lv)
        rig.run("FUN_00667430", 0x667430, build, model, 0)

    def pop_locations(self):
        """FUN_00666e00: turn each POP's corner pair into a position and size relative to the map, and its index into a pop number."""
        rig, at = self.rig, self.at

        def build(img, rng):
            """A map with an origin, a bounds array and an index array; the count is on the stack."""
            put(img, 0x10, rng.randrange(-5, 50))
            put(img, 0x14, rng.randrange(-5, 50))
            put(img, 0x50, at(0x200))
            put(img, 0x44, at(0x300))
            n = rng.choice((-1, 0, 1, 2, 4))
            for k in range(4):
                for f in range(4):
                    put(img, 0x200 + 0x10 * k + 4 * f, rng.randrange(-20, 20))
                put(img, 0x300 + 4 * k, rng.choice((0, 3, 4, 7, 8, -1, -5, -8, rng.randrange(-40, 40))))
            return {"regs": {"esi": at(0)}, "stack": [n]}

        def model(m, a):
            """Min/max the corners, offset by the map origin, size = span + 1; index = (index + (index < 0 ? 3 : 0)) >> 2 - 1."""
            mp = a["regs"]["esi"]
            n = s32(a["stack"][0])
            m.w(mp + 0x40, n)
            for i in range(max(n, 0)):
                e = m.r(mp + 0x50) + 0x10 * i
                x0, y0, x1, y1 = (s32(m.r(e + 4 * k)) for k in range(4))
                m.w(e, s32(m.r(mp + 0x10)) + min(x0, x1))
                m.w(e + 4, s32(m.r(mp + 0x14)) + min(y0, y1))
                m.w(e + 8, max(x0, x1) - min(x0, x1) + 1)
                m.w(e + 12, max(y0, y1) - min(y0, y1) + 1)
                slot = m.r(mp + 0x44) + 4 * i
                v = s32(m.r(slot))
                m.w(slot, ((v + (3 if v < 0 else 0)) >> 2) - 1)
        rig.run("FUN_00666e00", 0x666E00, build, model, 1, ret=False)
    return free_file, release, init_area, pop_locations


Suite.free_file, Suite.release, Suite.init_area, Suite.pop_locations = more_methods()


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 200
    s = Suite(rig)
    s.cache()
    s.tiles()
    s.free_file()
    s.release()
    s.init_area()
    s.pop_locations()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("no-ref", "fade", "keep-8", "ai-line", "no-file")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "Preset.cpp file cache and POP tile", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
