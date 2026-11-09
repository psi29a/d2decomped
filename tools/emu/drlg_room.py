# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify more helpers of DRLG\\DrlgRoom.cpp in stock game.exe 1.14d.

    uv run python drlg_room.py [--mutation-check]

Runs game.exe's own code for FUN_0066b4f0 and FUN_0066b530 (free a room's
list nodes), FUN_0066b610 and FUN_0066b670 (unlink a typed node), FUN_0066b790
(allocate an orth link), FUN_0066b900 (room gap test), FUN_0066ba20 (point in
a 5x5 tile rectangle), FUN_0066bac0 (warp destination level), FUN_0066bb20 and
FUN_0066bb60 (room flag tests), FUN_0066bba0, FUN_0066bcf0, FUN_0066bd00 (room2
near-array copy), FUN_0066bfd0 (level record bytes), FUN_0066c040 (level record
lookup by id) and FUN_0066c0b0 (room's act) against hand-written models on
random records (_dungeon_rig.py): return value, callee log, arena, allocator
blocks, fatal-error line and the bytes `ret n` pops. FUN_0066b670 runs
FUN_0066b610 for real. Hooked, logged, not claimed: FUN_0066b720,
FUN_0066ab00, FUN_0061aae0, FUN_0061e470, FUN_00667010 and the allocator.
`--mutation-check` breaks the model four ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt, s32

MUT = set()
FILE = 0x6E_EDF8
DEFAULT_RECORD = 0x6E_EE0C


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
        self.state = {"a": 0, "b": 0}
        rig.callee("c66b720", 0x66_B720, 1, ("ecx", "edx", "ebx"))
        rig.callee("c66ab00", 0x66_AB00, 2, ("ecx", "edx"), nlog=0)
        rig.callee("c61aae0", 0x61_AAE0, 0, ("ecx",))
        rig.callee("c61e470", 0x61_E470, 0, ("ecx",))
        rig.callee("c667010", 0x66_7010, 0, ("ecx",))
        rig.custom["c66ab00"] = lambda e: self.state["a"]
        rig.custom["c61aae0"] = lambda e: self.state["b"]
        rig.custom["c61e470"] = lambda e: self.base + 0x200
        rig.custom["c667010"] = lambda e: 0x1234_5678

    def at(self, off):
        """Arena address of offset `off`."""
        return self.base + off

    def lists(self):
        """FUN_0066b4f0, 0066b530, 0066b610, 0066b670."""
        rig, at = self.rig, self.at

        def b_nodes(img, rng):
            """A chain of 0..4 nodes at 0x300 (next at +4) hung off the room's +0x4c."""
            n = rng.randrange(0, 5)
            put(img, 0x4C, at(0x300) if n else 0)
            for i in range(n):
                put(img, 0x300 + 0x40 * i + 4, at(0x300 + 0x40 * (i + 1)) if i + 1 < n else 0)
            return {"ecx": rng.getrandbits(32), "edx": at(0)}

        def m_nodes(m, a):
            """Free each node of the +0x4c chain (line 0x58), then clear +0x4c."""
            node = m.r(a["edx"] + 0x4C)
            while node:
                nxt = m.r(node + 4)
                m.free(a["ecx"], node, 0x58, FILE)
                node = nxt
            m.w(a["edx"] + 0x4C, 0)
        rig.run("FUN_0066b4f0", 0x66B4F0, b_nodes, m_nodes, 0)

        def b_chain(img, rng):
            """A chain whose next pointer is at +0x14, passed as the head (or null)."""
            n = rng.randrange(0, 5)
            for i in range(n):
                put(img, 0x300 + 0x40 * i + 0x14, at(0x300 + 0x40 * (i + 1)) if i + 1 < n else 0)
            return {"ecx": rng.getrandbits(32), "edx": at(0x300) if n else 0}

        def m_chain(m, a):
            """Free every node of the chain (line 0xbd)."""
            node = a["edx"]
            while node:
                nxt = m.r(node + 0x14)
                m.free(a["ecx"], node, 0xBD, FILE)
                node = nxt
        rig.run("FUN_0066b530", 0x66B530, b_chain, m_chain, 0)

        def b_unlink(img, rng, key_slot=0x80):
            """A head cell at 0x10 pointing at 1..4 nodes {key +0, type +0xc, next +0x14}; a key to remove."""
            n = rng.randrange(1, 5)
            keys = [rng.randrange(0, 3) for _ in range(n)]
            put(img, 0x10, at(0x300))
            for i in range(n):
                put(img, 0x300 + 0x40 * i, keys[i])
                put(img, 0x300 + 0x40 * i + 0xC, rng.choice((1, 1, 2)))
                put(img, 0x300 + 0x40 * i + 0x14, at(0x300 + 0x40 * (i + 1)) if i + 1 < n else 0)
            return {"ecx": rng.getrandbits(32), "edx": at(0x10), "stack": [rng.randrange(0, 3)]}

        def unlink(m, pool, cell, key):
            """FUN_0066b610 on the model: drop the first type-1 node with this key, free it (line 0x115)."""
            head = m.r(cell)
            nxt = m.r(head + 0x14)
            if (m.r(head + 0xC) == 1 or mut("unlink-type")) and m.r(head) == key:
                m.w(cell, nxt)
                m.free(pool, head, 0x115, FILE)
                return
            prev, cur = head, nxt
            while cur:
                if m.r(cur + 0xC) == 1 and m.r(cur) == key:
                    m.w(prev + 0x14, m.r(cur + 0x14))
                    m.free(pool, cur, 0x115, FILE)
                    return
                prev, cur = cur, m.r(cur + 0x14)
        rig.run("FUN_0066b610", 0x66B610, b_unlink,
                lambda m, a: unlink(m, a["ecx"], a["edx"], a["stack"][0]), 1)

        def b_pair(img, rng):
            """Two head cells at 0x10 and 0x20, each with a short chain; keys are the other cell's address."""
            ca, cb = at(0x10), at(0x20)
            put(img, 0x10, at(0x300))
            put(img, 0x20, at(0x500))
            for base, other in ((0x300, cb), (0x500, ca)):
                n = rng.randrange(1, 4)
                for i in range(n):
                    put(img, base + 0x40 * i, rng.choice((ca, cb, 7)))
                    put(img, base + 0x40 * i + 0xC, rng.choice((1, 1, 2)))
                    put(img, base + 0x40 * i + 0x14, at(base + 0x40 * (i + 1)) if i + 1 < n else 0)
            return {"ecx": rng.getrandbits(32), "edx": ca, "stack": [cb]}

        def m_pair(m, a):
            """Unlink key b from list a, then key a from list b."""
            unlink(m, a["ecx"], a["edx"], a["stack"][0])
            unlink(m, a["ecx"], a["stack"][0], a["edx"])
        rig.run("FUN_0066b670", 0x66B670, b_pair, m_pair, 1)

    def rooms(self):
        """FUN_0066b790, 0066b900, 0066ba20, 0066bac0, 0066bb20, 0066bb60, 0066bba0, 0066bcf0, 0066bd00, 0066bfd0, 0066c040, 0066c0b0."""
        rig, at = self.rig, self.at

        def b_orth(img, rng):
            """A level whose act (+0x1b4) holds the pool at +0x478."""
            put(img, 0x1B4, at(0x600))
            return {"ecx": at(0x700), "edx": at(0), "stack": [rng.randrange(0, 4), rng.randrange(0, 2)]}

        def m_orth(m, a):
            """Allocate 0x18 (line 0x15f), fill {level, dir, preset, 0, level + 0x1c, 0}, FUN_0066b720(dir, preset, room**, ebx = orth)."""
            lv = a["edx"]
            d, flag = a["stack"]
            p = m.alloc(m.r(m.r(lv + 0x1B4) + 0x478), 0x18, 0x15F, FILE)
            for off, v in ((0x14, 0), (0, lv), (4, flag if mut("orth-swap") else d), (8, d if mut("orth-swap") else flag), (0xC, 0), (0x10, lv + 0x1C)):
                m.w(p + off, v)
            m.call("c66b720", d, flag, p, a["ecx"])
        rig.run("FUN_0066b790", 0x66B790, b_orth, m_orth, 2)

        def b_gap(img, rng):
            """A list at ecx + 0x10 of 0..4 rooms (rect x +0x34, y +0x38, w +0x3c, h +0x40), a room, a skip room."""
            n = rng.randrange(0, 5)
            put(img, 0x10, at(0x300) if n else 0)
            for i in range(n):
                put(img, 0x300 + 0x80 * i + 0x24, at(0x300 + 0x80 * (i + 1)) if i + 1 < n else 0)
                for o in (0x34, 0x38):
                    put(img, 0x300 + 0x80 * i + o, rng.randrange(0, 12))
                for o in (0x3C, 0x40):
                    put(img, 0x300 + 0x80 * i + o, rng.randrange(1, 5))
            for o in (0x34, 0x38):
                put(img, 0x700 + o, rng.randrange(0, 12))
            for o in (0x3C, 0x40):
                put(img, 0x700 + o, rng.randrange(1, 5))
            skip = rng.choice((0, at(0x300), at(0x380), at(0x400)))
            return {"ecx": at(0), "edx": at(0x700), "stack": [skip, rng.randrange(0, 6)]}

        def m_gap(m, a):
            """0 when some other listed room is within the gap on both axes, else 1."""
            r, skip, gap = a["edx"], a["stack"][0], a["stack"][1]
            node = m.r(a["ecx"] + 0x10)
            while node:
                if node != r and node != skip:
                    ax, bx = s32(m.r(r + 0x34)), s32(m.r(node + 0x34))
                    dx = bx - s32(m.r(r + 0x3C)) - ax if ax < bx else ax - s32(m.r(node + 0x3C)) - bx
                    ay, by = s32(m.r(r + 0x38)), s32(m.r(node + 0x38))
                    dy = by - s32(m.r(r + 0x40)) - ay if ay < by else ay - s32(m.r(node + 0x40)) - by
                    if dx < gap and dy < gap + (1 if mut("gap-y") else 0):
                        return 0
                node = m.r(node + 0x24)
            return 1
        rig.run("FUN_0066b900", 0x66B900, b_gap, m_gap, 2)

        def b_rect5(img, rng):
            """A rectangle {a, b, c, d} in 5-unit tiles and a point near it."""
            vals = [rng.randrange(0, 5), rng.randrange(0, 5), rng.randrange(0, 4), rng.randrange(0, 4)]
            for o, v in zip((0, 4, 8, 12), vals):
                put(img, o, v)
            a, b, c, d = vals
            return {"ecx": at(0), "edx": rng.randrange(5 * a - 1, 5 * a + 5 * c + 2) & M32,
                    "stack": [rng.randrange(5 * b - 1, 5 * b + 5 * d + 2) & M32]}

        def m_rect5(m, a):
            """5a <= x < 5a + 5c and 5b <= y < 5b + 5d, signed."""
            ra, rb, rc, rd = (m.r(a["ecx"] + o) for o in (0, 4, 8, 12))
            x, y = s32(a["edx"]), s32(a["stack"][0])
            return int(5 * s32(ra) <= x < 5 * s32(ra) + 5 * s32(rc) and 5 * s32(rb) <= y < 5 * s32(rb) + 5 * s32(rd) + (1 if mut("rect5-y") else 0))
        rig.run("FUN_0066ba20", 0x66BA20, b_rect5, m_rect5, 1)

        def b_dest(img, rng):
            """Hooked FUN_0066ab00 returns a room or null; FUN_0061aae0 a roomex or null; that one has a level or none."""
            lv = rng.choice((0, at(0x300)))
            put(img, 0x300 + 0x1D0, rng.getrandbits(32))
            put(img, 0x58 + 0x400, lv)
            self.state["a"] = rng.choice((0, at(0x800)))
            self.state["b"] = rng.choice((0, at(0x400)))
            return {"ecx": at(0), "edx": rng.getrandbits(32)}

        def m_dest(m, a):
            """Fatal 0x25c (no room), 0x25e (no roomex), 0x25f (no level); else the level id."""
            ra = m.call("c66ab00", a["ecx"], a["edx"])
            if ra == 0:
                raise Halt(0x25C)
            rx = m.call("c61aae0", ra)
            if rx == 0:
                raise Halt(0x25E)
            lv = m.r(rx + 0x58)
            if lv == 0:
                raise Halt(0x25F)
            return m.r(lv + 0x1D0)
        rig.run("FUN_0066bac0", 0x66BAC0, b_dest, m_dest, 0)

        def b_room(img, rng):
            """A room with flags and a level, or null."""
            put(img, 0x28, rng.choice((0, 0x800000, 0x30000, 0x10000, rng.getrandbits(32))))
            put(img, 0x58, at(0x300))
            put(img, 0x300 + 0x1D0, rng.getrandbits(32))
            return {"ecx": rng.choice((0, at(0), at(0)))}

        def m_nowarp(m, a):
            """Fatal 0x26b on null; 0 when flag 0x800000 is set, else the level id."""
            if a["ecx"] == 0:
                raise Halt(0x26B)
            if m.r(a["ecx"] + 0x28) & 0x800000:
                return 0
            return m.r(m.r(a["ecx"] + 0x58) + 0x1D0)
        rig.run("FUN_0066bb20", 0x66BB20, b_room, m_nowarp, 0)

        def m_wp(m, a):
            """Fatal 0x276 on null; else whether any of the waypoint bits 0x30000 is set."""
            if a["ecx"] == 0:
                raise Halt(0x276)
            return int(m.r(a["ecx"] + 0x28) & 0x30000 != 0)
        rig.run("FUN_0066bb60", 0x66BB60, b_room, m_wp, 0)

        def b_type(img, rng):
            """A room with preset type 0..3."""
            put(img, 0x48, rng.choice((0, 1, 2, 2, 3)))
            return {"ecx": at(0)}
        rig.run("FUN_0066bba0", 0x66BBA0, b_type,
                lambda m, a: m.call("c667010", a["ecx"]) if m.r(a["ecx"] + 0x48) == 2 else DEFAULT_RECORD, 0)

        rig.run("FUN_0066bcf0", 0x66BCF0, lambda img, rng: {"ecx": at(0)}, lambda m, a: m.r(a["ecx"] + 0x2C), 0)

        def b_near(img, rng):
            """A room2 with 0..6 neighbours; each neighbour's +0x30 is zero or a value; an output array at 0x900."""
            n = rng.randrange(0, 7)
            put(img, 0x2C, n)
            put(img, 8, at(0x600))
            for i in range(n):
                put(img, 0x600 + 4 * i, at(0x300 + 0x40 * i))
                put(img, 0x300 + 0x40 * i + 0x30, rng.choice((0, 0, rng.getrandbits(32))))
            return {"ecx": at(0), "edx": at(0x900)}

        def m_near(m, a):
            """Copy the nonzero +0x30 of each neighbour, zero-fill the rest, return the count copied."""
            r, out = a["ecx"], a["edx"]
            n, arr = m.r(r + 0x2C), m.r(r + 8)
            k = 0
            for i in range(n):
                v = m.r(m.r(arr + 4 * i) + 0x30)
                if v:
                    m.w(out + 4 * k, v)
                    k += 1
            for j in range(k, n):
                m.w(out + 4 * j, 0)
            return k
        rig.run("FUN_0066bd00", 0x66BD00, b_near, m_near, 0)

        def b_bytes(img, rng):
            """A room whose level id selects (hooked) a record at 0x200 with four bytes at +0x88; four output cells."""
            put(img, 0x58, at(0x300))
            put(img, 0x300 + 0x1D0, rng.getrandbits(32))
            return {"ecx": at(0), "edx": at(0x900), "stack": [at(0x910), at(0x920), at(0x930)]}

        def m_bytes(m, a):
            """Look the level up (FUN_0061e470) and copy the record's bytes +0x88..+0x8b to the four byte cells."""
            rec = m.call("c61e470", m.r(m.r(a["ecx"] + 0x58) + 0x1D0))
            m.w(a["edx"], m.r(rec + 0x88), 1)
            for i, p in enumerate(a["stack"]):
                m.w(p, m.r(rec + 0x89 + i), 1)
        rig.run("FUN_0066bfd0", 0x66BFD0, b_bytes, m_bytes, 3)

        def b_find(img, rng):
            """A list at +0x90 of 0..3 entries {id +0, data +4, next +0x44}; an id."""
            n = rng.randrange(0, 4)
            put(img, 0x90, at(0x300) if n else 0)
            for i in range(n):
                put(img, 0x300 + 0x80 * i, rng.choice((0, 1, 2, 3)))
                put(img, 0x300 + 0x80 * i + 0x44, at(0x300 + 0x80 * (i + 1)) if i + 1 < n else 0)
            return {"ecx": at(0), "edx": rng.randrange(0, 4)}

        def m_find(m, a):
            """The entry with this id: its +4; a zero id in the list is fatal 0x410; no match: FUN_0061e470(id) + 0x48."""
            node = m.r(a["ecx"] + 0x90)
            while node:
                ident = m.r(node)
                if ident == 0:
                    raise Halt(0x410)
                if ident == a["edx"]:
                    return node + 4
                node = m.r(node + 0x44)
            return m.call("c61e470", a["edx"]) + 0x48
        rig.run("FUN_0066c040", 0x66C040, b_find, m_find, 0)

        def b_act(img, rng):
            """A room whose level (or the level's act) may be null."""
            put(img, 0x58, rng.choice((0, at(0x300))))
            put(img, 0x300 + 0x1B4, rng.choice((0, at(0x600))))
            return {"ecx": rng.choice((0, at(0)))}

        def m_act(m, a):
            """Fatal 0x42b / 0x42c / 0x42d for a null room, level, act; else the act."""
            if a["ecx"] == 0:
                raise Halt(0x42B)
            lv = m.r(a["ecx"] + 0x58)
            if lv == 0:
                raise Halt(0x42C)
            act = m.r(lv + 0x1B4)
            if act == 0:
                raise Halt(0x42D)
            return act
        rig.run("FUN_0066c0b0", 0x66C0B0, b_act, m_act, 0)


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 80
    s = Suite(rig)
    s.lists()
    s.rooms()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("gap-y", "rect5-y", "orth-swap", "unlink-type")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "DrlgRoom.cpp helper", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
