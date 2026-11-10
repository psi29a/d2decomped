# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify more Drlg.cpp act and level helpers in stock game.exe 1.14d.

    uv run python drlg_act.py [--mutation-check]

Runs game.exe's own code for FUN_00642740 (returns 0), FUN_006427a0 (level id
holding a point), FUN_00642820 and FUN_00642840 (LevelDefs fields +0x94 and
+0x98), FUN_006429e0 (a byte of a table record), FUN_00642860 (warps info of
a level, built from LevelDefs), FUN_00642190 (free an act) and FUN_00642f70
(add to a level's active count and to its Vis levels') against hand-written
models on random records (_dungeon_rig.py): return value, callee log, arena,
allocator blocks and the bytes `ret n` pops. FUN_006427a0 runs FUN_0066b980
for real; FUN_00643020 (move a room between levels' active counts) runs in a
second rig that hooks FUN_00642f70. Hooked, logged, not claimed: FUN_0061e470,
FUN_0061eaa0, FUN_00642010, FUN_00642ae0, FUN_0066c040 and the allocator.
`--mutation-check` breaks the model three ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, s32

MUT = set()
FILE = 0x6E_B2E0


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
        rig.callee("c61e470", 0x61_E470, 0, ("ecx",))
        rig.callee("c61eaa0", 0x61_EAA0, 1, ())
        rig.callee("c642010", 0x64_2010, 2, ("esi",))
        rig.callee("c642ae0", 0x64_2AE0, 0, ("ecx", "edx"))
        rig.callee("c66c040", 0x66_C040, 0, ("ecx", "edx"))
        rig.custom["c61e470"] = lambda e: self.base + 0x300
        rig.custom["c61eaa0"] = lambda e: self.base + 0x300
        rig.custom["c642ae0"] = lambda e: self.base + 0xF00
        rig.custom["c66c040"] = lambda e: self.base + 0x690

    def at(self, off):
        """Arena address of offset `off`."""
        return self.base + off

    def simple(self):
        """FUN_00642740, 006427a0, 00642820, 00642840, 006429e0, 00642860."""
        rig, at = self.rig, self.at
        rig.run("FUN_00642740", 0x642740, lambda img, rng: {}, lambda m, a: 0, 0)

        def b_where(img, rng):
            """An act whose levels (list at +0x47c, next +0x1ac, rect at +0x1c, id +0x1d0) tile a small map."""
            n = rng.randrange(0, 4)
            put(img, 0x47C, at(0x700) if n else 0)
            for i in range(n):
                b = 0x700 + 0x200 * i
                put(img, b + 0x1AC, at(0x700 + 0x200 * (i + 1)) if i + 1 < n else 0)
                put(img, b + 0x1D0, rng.randrange(1, 100))
                for o in (0x1C, 0x20):
                    put(img, b + o, rng.randrange(0, 6))
                for o in (0x24, 0x28):
                    put(img, b + o, rng.randrange(1, 4))
            return {"ecx": at(0), "edx": rng.randrange(-1, 9) & M32, "stack": [rng.randrange(-1, 9) & M32]}

        def m_where(m, a):
            """The first level holding (x, y) in [x0, x0 + w) x [y0, y0 + h), else 0."""
            lv = m.r(a["ecx"] + 0x47C)
            x, y = s32(a["edx"]), s32(a["stack"][0])
            while lv:
                x0, y0, w, h = (s32(m.r(lv + o)) for o in (0x1C, 0x20, 0x24, 0x28))
                if x0 <= x < x0 + w and y0 <= y < y0 + h:
                    return m.r(lv + 0x1D0)
                lv = m.r(lv + 0x1AC)
            return 0
        rig.run("FUN_006427a0", 0x6427A0, b_where, m_where, 1)

        def b_id(img, rng):
            """A level id."""
            return {"stack": [rng.getrandbits(32)]}
        rig.run("FUN_00642820", 0x642820, b_id,
                lambda m, a: m.r(m.call("c61e470", a["stack"][0]) + 0x94), 1)
        rig.run("FUN_00642840", 0x642840, b_id,
                lambda m, a: m.r(m.call("c61e470", a["stack"][0]) + 0x98), 1)
        rig.run("FUN_006429e0", 0x6429E0, b_id,
                lambda m, a: m.r(m.call("c61eaa0", a["stack"][0]) + 0x780, 1), 1, mask=0xFF)

        def b_warps(img, rng):
            """An act with 0..3 warp-info entries {id +0, ..., next +0x44}; a level id to look for."""
            n = rng.randrange(0, 4)
            put(img, 0x90, at(0x700) if n else 0)
            put(img, 0x478, rng.getrandbits(32))
            for i in range(n):
                put(img, 0x700 + 0x80 * i, rng.randrange(0, 4))
                put(img, 0x700 + 0x80 * i + 0x44, at(0x700 + 0x80 * (i + 1)) if i + 1 < n else 0)
            return {"ecx": at(0), "edx": rng.randrange(0, 4)}

        def m_warps(m, a):
            """The entry for this level, else a new 0x48-byte one (line 0x3e0) with Vis and Warp rows of the LevelDefs line."""
            act, lid = a["ecx"], a["edx"]
            node = m.r(act + 0x90)
            while node:
                if m.r(node) == lid:
                    return node
                node = m.r(node + 0x44)
            rec = m.call("c61e470", lid)
            p = m.alloc(m.r(act + 0x478), 0x48, 0x3E0, FILE)
            m.w(p, lid)
            for k in range(8):
                m.w(p + 4 + 4 * k, m.r(rec + 0x48 + 4 * k))
                m.w(p + 0x24 + 4 * k, m.r(rec + 0x68 + 4 * (0 if mut("warps-col") else k)))
            m.w(p + 0x44, m.r(act + 0x90))
            m.w(act + 0x90, p)
            return p
        rig.run("FUN_00642860", 0x642860, b_warps, m_warps, 0)

    def lifecycle(self):
        """FUN_00642190 and FUN_00642f70."""
        rig, at = self.rig, self.at

        def b_free(img, rng):
            """An act with 0..3 levels and 0..3 warp-info entries."""
            put(img, 0x478, rng.getrandbits(32))
            n = rng.randrange(0, 4)
            put(img, 0x47C, at(0x700) if n else 0)
            for i in range(n):
                put(img, 0x700 + 0x200 * i + 0x1AC, at(0x700 + 0x200 * (i + 1)) if i + 1 < n else 0)
            k = rng.randrange(0, 4)
            put(img, 0x90, at(0x1000) if k else 0)
            for i in range(k):
                put(img, 0x1000 + 0x80 * i + 0x44, at(0x1000 + 0x80 * (i + 1)) if i + 1 < k else 0)
            return {"ecx": at(0)}

        def m_free(m, a):
            """Per level: FUN_00642010(level in ESI, pool, 0) then free (line 0xdd); the warps list (line 0xec); the act (line 0xf2)."""
            act = a["ecx"]
            pool = m.r(act + 0x478)
            lv = m.r(act + 0x47C)
            while lv:
                nxt = m.r(lv + 0x1AC)
                m.call("c642010", lv, m.r(act + 0x478), 0)
                m.free(m.r(act + 0x478), lv, 0xDD, FILE)
                lv = nxt
            m.w(act + 0x47C, 0)
            node = m.r(act + 0x90)
            while node:
                nxt = m.r(node + 0x44)
                m.free(pool, node, 0xEC, FILE)
                node = nxt
            m.w(act + 0x90, 0)
            m.free(pool, act, 0xF2, FILE)
        rig.run("FUN_00642190", 0x642190, b_free, m_free, 0)

        def b_active(img, rng):
            """A level at 0 in an act at 0x200; the act's level list; a Vis array at 0x690 (the hook's answer)."""
            put(img, 0x1B4, at(0x200))
            put(img, 0x1D0, rng.randrange(1, 50))
            put(img, 0xC, rng.randrange(-2, 5))
            n = rng.randrange(0, 3)
            put(img, 0x200 + 0x47C, at(0x700) if n else 0)
            ids = [rng.choice((0, 0, 7, 8, 9)) for _ in range(8)]
            for i in range(8):
                put(img, 0x690 + 4 * i, ids[i])
            for i in range(n):
                put(img, 0x700 + 0x200 * i + 0x1AC, at(0x700 + 0x200 * (i + 1)) if i + 1 < n else 0)
                put(img, 0x700 + 0x200 * i + 0x1D0, rng.choice((7, 8, 9)))
                put(img, 0x700 + 0x200 * i + 0xC, rng.randrange(-2, 5))
            put(img, 0xF00 + 0xC, rng.randrange(-2, 5))
            return {"ecx": at(0), "edx": rng.choice((1, -1, 2, -3, 0)) & M32}

        def bump(m, lv, cnt):
            """+0xc += count, +0x1d4 = 10, negative or zero results become 0."""
            v = s32(m.r(lv + 0xC) + cnt)
            m.w(lv + 0x1D4, 10)
            m.w(lv + 0xC, v if v > 0 else (1 if mut("keep-zero") else 0))

        def m_active(m, a):
            """Bump the level, then each nonzero Vis level (found on the act's list, else FUN_00642ae0)."""
            lv, cnt = a["ecx"], a["edx"]
            bump(m, lv, cnt)
            act = m.r(lv + 0x1B4)
            arr = m.call("c66c040", act, m.r(lv + 0x1D0))
            for i in range(8):
                vid = m.r(arr + 4 * i)
                if not vid:
                    continue
                node = m.r(act + 0x47C)
                while node and m.r(node + 0x1D0) != vid:
                    node = m.r(node + 0x1AC)
                if not node:
                    node = m.call("c642ae0", act, vid)
                bump(m, node, cnt)
        rig.run("FUN_00642f70", 0x642F70, b_active, m_active, 0)

    def moves(self):
        """FUN_00643020 (run on a rig that hooks FUN_00642f70)."""
        rig, at = self.rig, self.at
        rig.callee("c642f70", 0x64_2F70, 0, ("ecx", "edx"))

        def b_move(img, rng):
            """Two rooms (either may be null) whose levels may be the same."""
            put(img, 0x58, at(0x300))
            put(img, 0x100 + 0x58, rng.choice((at(0x300), at(0x300), at(0x400), 0)))
            return {"ecx": rng.choice((0, at(0), at(0))), "edx": rng.choice((0, at(0x100), at(0x100)))}

        def m_move(m, a):
            """When the levels differ: -1 on the first room's level, +1 on the second's."""
            ra, rb = a["ecx"], a["edx"]
            la = m.r(ra + 0x58) if ra else 0
            lb = m.r(rb + 0x58) if rb else 0
            if la != lb:
                if la:
                    m.call("c642f70", la, M32)
                if lb:
                    m.call("c642f70", lb, 1 if not mut("move-sign") else M32)
        rig.run("FUN_00643020", 0x643020, b_move, m_move, 0)


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 80
    s = Suite(rig)
    s.simple()
    s.lifecycle()
    second = rig_mod.Rig()
    Suite(second).moves()
    return rig.cases + second.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("warps-col", "keep-zero", "move-sign")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "Drlg.cpp act and level helper", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
