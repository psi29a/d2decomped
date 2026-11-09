# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify DRLG path helpers and OutPlace edge-border coordinates in stock game.exe 1.14d.

    uv run python drlg_path.py [--mutation-check]

Runs game.exe's own code for FUN_00675de0, FUN_00675eb0, FUN_0067a4a0,
FUN_0067a690 and FUN_0067a9f0 against hand-written models on random records
(_dungeon_rig.py): return value, callee log, arena and the bytes `ret n`
pops. The CRT memcpy in FUN_0067a9f0 runs for real, and the 32-entry
direction table at 0x6f18d8 is read from the emulator. Hooked, logged, not
claimed: FUN_0064d910, FUN_0067a420 and FUN_0067a630. `--mutation-check`
breaks the model four ways.
"""
import argparse
import struct
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, s32

MUT = set()


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def put(img, off, value):
    """Write a dword at arena offset `off`."""
    img[off:off + 4] = (value & M32).to_bytes(4, "little")


def s16(v):
    """Signed view of a 16-bit value."""
    return v - 0x10000 if v & 0x8000 else v


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig):
        """Hook the callees this oracle leaves unclaimed."""
        self.rig = rig
        self.base = rig.arena
        self.table = struct.unpack("<32I", rig.m.read(0x6F_18D8, 128))
        self.ctx = rig.m.alloc(0x37100)
        rig.callee("c64d910", 0x64_D910, 5, ())
        rig.callee("c67a420", 0x67_A420, 2, ("edi",))
        rig.callee("c67a630", 0x67_A630, 0, ("eax", "esi"))
        rig.custom["c64d910"] = lambda e: rig.rng.choice((0, 0, 0, 1))
        rig.custom["c67a420"] = lambda e: rig.rng.choice((0, 1, 1, 1))

    def at(self, off):
        """Arena address of offset `off`."""
        return self.base + off

    def edges(self):
        """FUN_00675de0 and FUN_00675eb0: output rectangle position beside a level rectangle, by direction and variant."""
        rig, at = self.rig, self.at

        def build(img, rng):
            """A level rectangle at 0x40 and an output rectangle at 0x80 (only its size is read); variant and direction."""
            for k in range(4):
                put(img, 0x40 + 4 * k, rng.randrange(0, 100))
                put(img, 0x80 + 4 * k, rng.randrange(0, 100))
            return {"regs": {"eax": at(0x80)}, "ecx": at(0x40), "edx": rng.randrange(0, 5), "stack": [rng.randrange(0, 5)]}

        def north_first(m, a):
            """FUN_00675de0: south edge x from the left, west edge y from the top, north edge from the right, east edge from the bottom."""
            out, lv, var, d = a["regs"]["eax"], a["ecx"], a["edx"], a["stack"][0]
            lx, ly, lw, lh = (m.r(lv + 4 * k) for k in range(4))
            ow, oh = m.r(out + 8), m.r(out + 12)
            if d == 0:
                m.w(out, lx)
                m.w(out + 4, lh + ly)
                if var == 1:
                    m.w(out, lx - 0x10)
            elif d == 1:
                m.w(out, lx - ow)
                m.w(out + 4, ly)
                if var == 1:
                    m.w(out + 4, ly - 0x10)
                elif var == 2:
                    m.w(out + 4, ly + 8)
            elif d == 2:
                m.w(out, lw - ow + lx)
                m.w(out + 4, ly - oh)
                if var == 1:
                    m.w(out, lw - ow + lx + 0x10)
            elif d == 3:
                m.w(out, lw + lx)
                y = ly + lh - oh
                m.w(out + 4, y)
                if var == 1:
                    m.w(out + 4, y + 0x10)
                elif var == 2:
                    m.w(out + 4, y - (8 if not mut("edge-8") else 7))
                elif var == 3:
                    m.w(out + 4, y + 8)
        rig.run("FUN_00675de0", 0x675DE0, build, north_first, 1, ret=False)

        def mirrored(m, a):
            """FUN_00675eb0: the same four edges with the roles of left and right, top and bottom swapped."""
            out, lv, var, d = a["regs"]["eax"], a["ecx"], a["edx"], a["stack"][0]
            lx, ly, lw, lh = (m.r(lv + 4 * k) for k in range(4))
            ow, oh = m.r(out + 8), m.r(out + 12)
            if d == 0:
                m.w(out, lw - ow + lx)
                m.w(out + 4, lh + ly)
                if var == 1:
                    m.w(out, lw - ow + lx + 0x10)
            elif d == 1:
                m.w(out, lx - ow)
                y = ly + lh - oh
                m.w(out + 4, y)
                if var == 1:
                    m.w(out + 4, y + 0x10)
                elif var == 2:
                    m.w(out + 4, y - 8)
            elif d == 2:
                m.w(out, lx)
                m.w(out + 4, ly - oh)
                if var == 1:
                    m.w(out, lx - 0x10)
            elif d == 3:
                m.w(out, lx + lw)
                m.w(out + 4, ly)
                if var == 1:
                    m.w(out + 4, ly - 0x10)
                elif var == 2:
                    m.w(out + 4, ly + 8)
                elif var == 3:
                    m.w(out + 4, ly - 8)
        rig.run("FUN_00675eb0", 0x675EB0, build, mirrored, 1, ret=False)

    def validate(self):
        """FUN_0067a4a0: every hop of a path is clear (adjacent tiles by a collision test, longer hops by a ray test)."""
        rig, at = self.rig, self.at

        def build(img, rng):
            """A path argument with a start point and a dynamic path of up to six waypoints."""
            put(img, 0, rng.randrange(0, 20) | rng.randrange(0, 20) << 16)
            put(img, 0x30, at(0x100))
            put(img, 0x100 + 0x1C, rng.getrandbits(32))
            put(img, 0x100 + 0x28, rng.choice((-1, 0, 1, 2, 4, 6)))
            put(img, 0x100 + 0x48, rng.getrandbits(32))
            put(img, 0x100 + 0x50, rng.getrandbits(32))
            prev = [img[0] | img[1] << 8, img[2] | img[3] << 8]
            for k in range(6):
                x = max(prev[0] + rng.choice((-3, -2, -1, 0, 1, 1, 2, 3)), 0)
                y = max(prev[1] + rng.choice((-3, -2, -1, 0, 1, 1, 2, 3)), 0)
                put(img, 0x100 + 0x9C + 4 * k, x | y << 16)
                prev = [x, y]
            return {"ecx": at(0)}

        def model(m, a):
            """Walk the waypoints; near hops use the collision test, far hops the ray test; the first blocked hop fails the path."""
            pta = a["ecx"]
            prev = m.r(pta)
            dyn = m.r(pta + 0x30)
            room, width, pattern = m.r(dyn + 0x1C), m.r(dyn + 0x48), m.r(dyn + 0x50)
            for i in range(max(s32(m.r(dyn + 0x28)), 0)):
                pt = m.r(dyn + 0x9C + 4 * i)
                near = abs((pt & 0xFFFF) - (prev & 0xFFFF)) < 2 and abs((pt >> 16) - (prev >> 16)) < (2 if not mut("far") else 3)
                if near:
                    if m.call("c64d910", room, pt & 0xFFFF, pt >> 16, width, pattern):
                        return 0
                elif not m.call("c67a420", dyn, prev, pt):
                    return 0
                prev = pt
            return 1
        rig.run("FUN_0067a4a0", 0x67A4A0, build, model, 0)

    def advance(self):
        """FUN_0067a690: step a path node's visit count and direction, climbing to the parent at five."""
        rig, at = self.rig, self.at
        mem = rig.m.mu.mem_write
        ctx = self.ctx
        # arena: seed 0x40, nodes 0x100 + 0x20 k (k = 0 is the root), seed-pointer arrays from 0x800

        def build(img, rng):
            """A chain of up to five nodes with flags near the limit; the context decides how directions are picked."""
            put(img, 0x40, rng.getrandbits(32))
            put(img, 0x44, rng.getrandbits(32))
            depth = rng.randrange(1, 5)
            for k in range(depth):
                node = 0x100 + 0x20 * k
                put(img, node + 6, 0)
                img[node + 6:node + 8] = rng.choice((0, 3, 4, 4, 5, 0xFFFF, 1)).to_bytes(2, "little") if k == depth - 1 else rng.choice((3, 4, 4, 5)).to_bytes(2, "little")
                put(img, node + 0xC, at(0x800 + 0x20 * k + 4 * rng.randrange(0, 4)))
                put(img, node + 0x10, rng.getrandbits(32))
                put(img, node + 0x14, at(0x100 + 0x20 * (k - 1)) if k else 0)
            for k in range(0x40):
                put(img, 0x800 + 4 * k, rng.getrandbits(32))
            flag = rng.choice((0, 1))
            mem(ctx, at(0x100).to_bytes(4, "little"))
            mem(ctx + 0x36FD0, flag.to_bytes(4, "little") + at(0x40).to_bytes(4, "little"))
            self.flag = flag
            return {"regs": {"eax": at(0x100 + 0x20 * (depth - 1))}, "stack": [ctx]}

        def model(m, a):
            """Count a visit (and re-roll the direction below four); at five climb to the parent and re-roll its direction."""
            node, c = a["regs"]["eax"], a["stack"][0]
            m.regions.append((c, bytearray(at(0x100).to_bytes(4, "little"))))
            m.regions.append((c + 0x36FD0, bytearray(self.flag.to_bytes(4, "little") + at(0x40).to_bytes(4, "little"))))
            head = at(0x100)
            if s16(m.r(node + 6, 2)) < (4 if not mut("below") else 5):
                m.w(node + 0xC, m.r(node + 0xC) + 4)
                m.call("c67a630", c, node)
            m.w(node + 6, (m.r(node + 6, 2) + 1) & 0xFFFF, 2)
            while m.r(node + 6, 2) == 5:
                if node == head:
                    return 0
                node = m.r(node + 0x14)
                m.w(node + 0xC, m.r(node + 0xC) + 4)
                step = m.r(m.r(node + 0xC))
                if self.flag == 0:
                    m.w(node + 0x10, (m.r(node + 0x10) + step) & 7)
                else:
                    seed = at(0x40)
                    lo, hi = m.r(seed), m.r(seed + 4)
                    new = lo * 0x6AC690C5 + hi
                    m.w(seed, new & M32)
                    m.w(seed + 4, (new >> 32) & M32)
                    m.w(node + 0x10, (self.table[new & 0x1F] + step + m.r(node + 0x10)) & 7)
                m.w(node + 6, (m.r(node + 6, 2) + 1) & 0xFFFF, 2)
            return node
        rig.run("FUN_0067a690", 0x67A690, build, model, 1)

    def extract(self):
        """FUN_0067a9f0: copy a search result's waypoints (direction changes only) from start to goal."""
        rig, at = self.rig, self.at

        def build(img, rng):
            """A chain of nodes from the goal back to the start, with runs of equal steps."""
            put(img, 0x30, at(0x100))
            n = rng.choice((0, 1, 2, 3, 8, 30, 60, 77, 78, 79, 80, 100))
            x, y = rng.randrange(40, 60), rng.randrange(40, 60)
            step = (1, 0)
            nodes = []
            for k in range(n):
                nodes.append((x, y))
                if n >= 70:
                    step = (1, 0) if k % 2 else (0, 1)      # every node a turn: n - 1 waypoints
                elif rng.random() >= 0.85:
                    step = rng.choice(((1, 0), (0, 1), (1, 1), (-1, 0), (0, -1), (-1, -1)))
                x, y = x + step[0], y + step[1]
            for k, (nx, ny) in enumerate(nodes):
                base = 0x400 + 0x20 * k
                put(img, base + 8, nx | ny << 16)
                put(img, base + 0x14, at(base + 0x20) if k + 1 < n else 0)
            return {"ecx": at(0), "regs": {"eax": at(0x400) if n else 0}}

        def model(m, a):
            """Walk parents from the goal noting each turn; with 2..77 turns copy them reversed into the path point array."""
            node = a["regs"]["eax"]
            dst = m.r(a["ecx"] + 0x30) + 0x9C
            if node == 0:
                return 0
            items = []
            pdx = pdy = -2
            while node:
                if len(items) >= 0x4E:
                    break
                parent = m.r(node + 0x14)
                if parent:
                    dx = m.r(node + 8, 2) - m.r(parent + 8, 2)
                    dy = m.r(node + 10, 2) - m.r(parent + 10, 2)
                    if dx != pdx or dy != pdy:
                        items.append(m.r(node + 8))
                        pdx, pdy = dx, dy
                node = parent
            n = len(items)
            if not 2 <= n <= 0x4D - (1 if mut("count") else 0):
                return 0
            for i, v in enumerate(reversed(items)):
                m.w(dst + 4 * i, v)
            return n
        rig.run("FUN_0067a9f0", 0x67A9F0, build, model, 0)


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 200
    s = Suite(rig)
    s.edges()
    s.validate()
    s.advance()
    s.extract()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("edge-8", "far", "below", "count")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "DRLG path and edge helper", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
