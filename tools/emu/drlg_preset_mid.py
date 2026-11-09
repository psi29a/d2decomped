# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the Preset.cpp file cache and POP tile updaters in stock game.exe 1.14d.

    uv run python drlg_preset_mid.py [--mutation-check]

Runs game.exe's own code for FUN_00665f40, FUN_00667060 and FUN_006671e0
against hand-written models on random records (_dungeon_rig.py): return
value, callee log, arena, allocator blocks, fatal-error line, the cache's
global head (0x96d60c) and the bytes `ret n` pops. The CRT memset runs for
real, with an allocator that returns dirty memory for the file record. The
import stubs give GetTickCount 0 and make InterlockedIncrement add one. Hooked, logged, not
claimed: FUN_00665950, FUN_0066b980, FUN_00604b90, FUN_00667060 (while
FUN_006671e0 runs) and the allocator. `--mutation-check` breaks the model
three ways.
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


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 200
    s = Suite(rig)
    s.cache()
    s.tiles()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("no-ref", "fade", "keep-8")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "Preset.cpp file cache and POP tile", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
