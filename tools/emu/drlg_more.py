# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 10 DRLG record, teardown and list helpers (Preset, DrlgRoom, DrlgLogic, Maze, Level) in stock game.exe 1.14d.

    uv run python drlg_more.py [--mutation-check]

Runs game.exe's own code for FUN_006660b0, FUN_006665b0, FUN_0066c6e0,
FUN_0066e160, FUN_00670810, FUN_00670c10, FUN_0067b1c0, FUN_0067d1e0,
FUN_0067d610 and FUN_0067d680 against hand-written models on random records
(_dungeon_rig.py): return value, callee log, arena, allocator blocks, fatal-error
line and the bytes `ret n` pops. FUN_0067cd90 (free a block and clear its slot)
runs for real inside the teardowns, and so do the CRT memcpy and memset, with an
allocator that returns dirty memory. Hooked, logged, not claimed: FUN_00666520,
FUN_0061f310 and the allocator.
`--mutation-check` breaks the model six ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt

MUT = set()
F_PRE, F_ROOM, F_RING, F_BUF, F_MAZE = 0x6E_E7C4, 0x6E_EEF0, 0x6F_1F54, 0x6F_1F40, 0x6F_1F68
LVL, ACT = 0x1000, 0x1800


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def put(img, off, value):
    """Write a dword at arena offset `off`."""
    img[off:off + 4] = (value & M32).to_bytes(4, "little")


def pool_of(m, room):
    """The act's allocator pool: [[[room + 0x58] + 0x1b4] + 0x478]."""
    return m.r(m.r(m.r(room + 0x58) + 0x1B4) + 0x478)


def m_clear(m, pool, slot):
    """FUN_0067cd90: free the block at slot + 4 (line 0x218) when set, then zero both dwords."""
    p = m.r(slot + 4)
    if p:
        m.free(pool, p, 0x218, F_BUF)
    m.w(slot + 4, 0)
    m.w(slot, 0)


def m_ring(m, pool, slot):
    """FUN_0067d1e0: free the ring hanging off the head at [slot] (lines 0x114 / 0x117 / 0x11f), then zero the slot."""
    head = m.r(slot)
    if head:
        cur = m.r(head + 0x10)
        done = False
        while cur:
            nxt = m.r(cur + 0x10)
            m.free(pool, cur, 0x114, F_RING)
            if nxt == head:
                m.free(pool, nxt, 0x117 + (1 if mut("ring-line") else 0), F_RING)
                done = True
                break
            cur = nxt
        if not done:
            m.free(pool, head, 0x11F, F_RING)
        m.w(slot, 0)


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig):
        """Hook the callees this oracle leaves unclaimed and give the allocator dirty memory."""
        self.rig = rig
        rig.callee("c666520", 0x66_6520, 0, ("eax",))
        rig.callee("c61f310", 0x61_F310, 0, ("ecx", "edx"))

        def dirty(e):
            """Allocator stand-in returning dirty memory, so skipped writes are visible."""
            from unicorn.x86_const import UC_X86_REG_EDX
            size = max(e.mu.reg_read(UC_X86_REG_EDX), 1)
            p = e.alloc(size)
            e.mu.mem_write(p, b"\xAA" * size)
            return p
        rig.custom["alloc"] = dirty

    def at(self, off):
        """Arena address of offset `off`."""
        return self.rig.arena + off

    def ring(self, img, rng, head_off):
        """A circular or terminated list at arena offset head_off (head) and head_off + 0x40.. (nodes); returns the head or 0."""
        if rng.random() < 0.15:
            return 0
        n = 0 if rng.random() < 0.1 else rng.randrange(0, 4)
        nodes = [self.at(head_off + 0x40 + 0x20 * i) for i in range(n)]
        head = self.at(head_off)
        chain = nodes + [head if rng.random() < 0.7 else 0]
        if n == 0 and rng.random() < 0.5:
            chain = [head]
        put(img, head_off + 0x10, chain[0])
        for i in range(len(nodes)):
            put(img, head_off + 0x40 + 0x20 * i + 0x10, chain[i + 1])
        return head

    def chain(self, img):
        """Room (offset 0) -> level (LVL) -> act (ACT) pointers, so pool_of works."""
        put(img, 0x58, self.at(LVL))
        put(img, LVL + 0x1B4, self.at(ACT))

    def records(self):
        """FUN_006660b0, 006665b0, 0066c6e0, 0066e160, 00670810, 00670c10, 0067b1c0."""
        rig, at = self.rig, self.at

        def b_clone(img, rng):
            """A record {count, rows} (12-byte rows) whose rows are at 0x400; any pool."""
            put(img, 0, rng.randrange(0, 7))
            put(img, 4, at(0x400))
            return {"ecx": rng.getrandbits(32), "edx": at(0)}

        def m_clone(m, a):
            """Allocate 8 (line 0x574), then 12 * count (0x579), copy the rows; return {count, rows copy}."""
            cnt = m.r(a["edx"])
            p = m.alloc(a["ecx"], 8, 0x574, F_PRE)
            q = m.alloc(a["ecx"], 12 * cnt, 0x579, F_PRE)
            m.w(p, cnt)
            m.w(p + 4, q)
            src = m.r(a["edx"] + 4)
            for k in range(12 * cnt - (4 if mut("clone-len") and cnt else 0)):
                m.w(q + k, m.r(src + k, 1), 1)
            return p
        rig.run("FUN_006660b0", 0x6660B0, b_clone, m_clone, 0)

        def b_pre(img, rng):
            """A room whose preset record (+0x20) may be null; a level and act pool behind it."""
            self.chain(img)
            put(img, 0x20, rng.choice((0, at(0x300))))
            put(img, 0x300 + 0xF0, rng.choice((0, rng.getrandbits(32))))
            return {"ecx": at(0)}

        def m_pre(m, a):
            """When the record exists: FUN_00666520(room), free +0xf0 (line 0x6f7) if set, free the record (0x6fa), clear +0x20."""
            r = a["ecx"]
            pool, rec = pool_of(m, r), m.r(r + 0x20)
            if rec:
                m.call("c666520", r)
                if m.r(rec + 0xF0):
                    m.free(pool, m.r(rec + 0xF0), 0x6F7, F_PRE)
                m.free(pool, rec, 0x6FA + (1 if mut("pre-line") else 0), F_PRE)
                m.w(r + 0x20, 0)
        rig.run("FUN_006665b0", 0x6665B0, b_pre, m_pre, 0)

        def b_room(img, rng):
            """A room whose logic record (+0x64) may be null, with a flag word and a chain of 0..3 blocks at +0x30."""
            self.chain(img)
            rec = rng.choice((0, at(0x300)))
            put(img, 0x64, rec)
            n = rng.randrange(0, 4)
            nodes = [at(0x500 + 0x40 * i) for i in range(n)]
            put(img, 0x300 + 0x30, nodes[0] if nodes else 0)
            for i, node in enumerate(nodes):
                put(img, 0x500 + 0x40 * i + 0x2C, nodes[i + 1] if i + 1 < n else 0)
            return {"ecx": at(0)}

        def m_room(m, a):
            """Clear the two buffers (bit 1 of +0), free the +0x30 chain through +0x2c (0x101), free the record (0x106), clear +0x64."""
            r = a["ecx"]
            pool, rec = pool_of(m, r), m.r(r + 0x64)
            if rec:
                if m.r(rec) & 2:
                    m_clear(m, pool, rec + 8)
                    m_clear(m, pool, rec + 0x1C)
                node = m.r(rec + 0x30)
                while node:
                    nxt = m.r(node + 0x2C)
                    m.free(pool, node, 0x101, F_ROOM)
                    node = nxt
                m.free(pool, rec, 0x106, F_ROOM)
                m.w(r + 0x64, 0)
        rig.run("FUN_0066c6e0", 0x66C6E0, b_room, m_room, 0)

        def b_lvl(img, rng):
            """An object (ESI, may be 0) with a record (+0x10, may be 0) whose byte +0x2c picks the branch; an arg of 0xb or not."""
            put(img, 0x10, rng.choice((0, 0, at(0x300), at(0x300), at(0x300))))
            img[0x300 + 0x2C] = rng.choice((0x62, 0x6C, 0x72, 0x6C, 0x72, rng.getrandbits(8)))
            return {"regs": {"esi": rng.choice((0, at(0), at(0), at(0), at(0)))}, "edx": rng.getrandbits(32),
                    "stack": [rng.choice((0xB, 0xB, 0, 5))]}

        def m_lvl(m, a):
            """Fatal 0x273 without the object, 0x274 without the record; else when the type byte is not 0x62 or 0x6c / 0x72 (arg 0xb): +0x10 = FUN_0061f310(record +0)."""
            o = a["regs"]["esi"]
            if o == 0:
                raise Halt(0x273)
            rec = m.r(o + 0x10)
            if rec == 0:
                raise Halt(0x274)
            cl = m.r(rec + 0x2C, 1)
            want = 0x6C if a["stack"][0] != 0xB else 0x72
            if mut("lvl-arg"):
                want = 0x72 if a["stack"][0] != 0xB else 0x6C
            if cl != 0x62 and cl != want:
                m.w(o + 0x10, m.call("c61f310", m.r(rec), (a["edx"] & ~0xFF & M32) | want))
        rig.run("FUN_0066e160", 0x66E160, b_lvl, m_lvl, 1)

        def b_act(img, rng):
            """A level (ECX, may be 0) with a table (+0x14, may be 0) and an act whose byte +0x450 indexes it."""
            self.chain(img)
            put(img, 0x14, rng.choice((0, at(0x300), at(0x300), at(0x300))))
            for k in range(0x100):
                put(img, 0x300 + 4 * k, rng.choice((0, 1, 1, 2)))
            img[0x1800 + 0x450] = rng.choice((0, 1, 2, 3, 5, rng.getrandbits(8)))
            put(img, 0x1B4, at(ACT))
            return {"ecx": rng.choice((0, at(0), at(0), at(0)))}

        def m_act(m, a):
            """Fatal 0x1c without the level, 0x1e without the table; else table[act byte + 1] == 1."""
            lvl = a["ecx"]
            if lvl == 0:
                raise Halt(0x1C)
            tab = m.r(lvl + 0x14)
            if tab == 0:
                raise Halt(0x1E)
            idx = m.r(m.r(lvl + 0x1B4) + 0x450, 1)
            return int(m.r(tab + idx * 4 + 4) == 1)
        rig.run("FUN_00670810", 0x670810, b_act, m_act, 0)

        def b_find(img, rng):
            """A list (EAX +0x10) of 0..4 nodes, each with a record (+0x20: key, a, b, flags +0xc) and a next (+0x24); key in ESI."""
            n = rng.randrange(0, 5)
            nodes = [at(0x400 + 0x30 * i) for i in range(n)]
            put(img, 0x10, nodes[0] if nodes else 0)
            for i in range(n):
                put(img, 0x400 + 0x30 * i + 0x20, at(0x800 + 0x20 * i))
                put(img, 0x400 + 0x30 * i + 0x24, nodes[i + 1] if i + 1 < n else 0)
                put(img, 0x800 + 0x20 * i, rng.randrange(0, 3))
            return {"regs": {"eax": at(0), "esi": rng.randrange(0, 3)}, "stack": [rng.getrandbits(32), rng.getrandbits(32), rng.choice((0, 1, 7))]}

        def m_find(m, a):
            """First node whose record has bit 1 of +0xc clear and key == ESI: rec = {a, b}, flag nonzero clears bit 1 else sets it; return the node, else 0."""
            node = m.r(a["regs"]["eax"] + 0x10)
            while node:
                rec = m.r(node + 0x20)
                flags = m.r(rec + 0xC)
                if (mut("find-flag") or not flags & 2) and m.r(rec) == a["regs"]["esi"]:
                    m.w(rec, a["stack"][0])
                    m.w(rec + 4, a["stack"][1])
                    m.w(rec + 0xC, (flags & ~2) if a["stack"][2] else (flags | 2))
                    return node
                node = m.r(node + 0x24)
            return 0
        rig.run("FUN_00670c10", 0x670C10, b_find, m_find, 3)

        def b_slot(img, rng):
            """An object with a used-count (+0x2fc4) up to the limit 0xc8; 0x38-byte slots from +0x404."""
            put(img, 0x2FC4, rng.choice((0xC8, 0, 1, rng.randrange(0, 0xC9))))
            return {"ecx": at(0)}

        def m_slot(m, a):
            """At 0xc8 slots return 0; else bump the count, zero the 0x38-byte slot at +0x404 + 0x38 * n and return it."""
            o = a["ecx"]
            n = m.r(o + 0x2FC4)
            if n == 0xC8 - (1 if mut("slot-cap") else 0):
                return 0
            m.w(o + 0x2FC4, n + 1)
            for k in range(0x38):
                m.w(o + 0x404 + 0x38 * n + k, 0, 1)
            return o + 0x404 + 0x38 * n
        rig.run("FUN_0067b1c0", 0x67B1C0, b_slot, m_slot, 0)

    def maze(self):
        """FUN_0067d1e0, 0067d610, 0067d680 (with FUN_0067cd90 for real)."""
        rig, at = self.rig, self.at

        def b_ring(img, rng):
            """A pool (ECX), a slot (EDX) at 0x300 holding a ring head or 0; the ring's nodes after it."""
            put(img, 0x300, self.ring(img, rng, 0x500))
            return {"ecx": rng.getrandbits(32), "edx": at(0x300)}

        def m_ring_fn(m, a):
            """Free the ring through +0x10, the head last, and clear the slot."""
            m_ring(m, a["ecx"], a["edx"])
        rig.run("FUN_0067d1e0", 0x67D1E0, b_ring, m_ring_fn, 0)

        def b_maze(img, rng):
            """A room whose maze record (+0x20, may be null) holds four buffers (+0, 0x14, 0x28, 0x3c) and a ring at +0x50."""
            self.chain(img)
            put(img, 0x20, rng.choice((0, at(0x300), at(0x300), at(0x300))))
            for off in (0, 0x14, 0x28, 0x3C):
                put(img, 0x300 + off + 4, rng.choice((0, rng.getrandbits(32))))
            put(img, 0x300 + 0x50, self.ring(img, rng, 0x500))
            return {"ecx": at(0)}

        def m_clear_all(m, pool, rec):
            """The four buffers are freed and cleared, then the ring."""
            for off in (0, 0x14, 0x28, 0x3C):
                m_clear(m, pool, rec + off)
            m_ring(m, pool, rec + 0x50)

        def m_free_maze(m, a):
            """Clear the record's parts, free it (line 0xad) and clear +0x20."""
            r = a["ecx"]
            pool, rec = pool_of(m, r), m.r(r + 0x20)
            if rec:
                m_clear_all(m, pool, rec)
                m.free(pool, rec, 0xAD, F_MAZE)
                m.w(r + 0x20, 0)
        rig.run("FUN_0067d610", 0x67D610, b_maze, m_free_maze, 0)

        def m_clear_maze(m, a):
            """Clear the record's parts, leave the record and +0x20."""
            r = a["ecx"]
            rec = m.r(r + 0x20)
            if rec:
                m_clear_all(m, pool_of(m, r), rec)
        rig.run("FUN_0067d680", 0x67D680, b_maze, m_clear_maze, 0)


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 80
    s = Suite(rig)
    s.records()
    s.maze()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("clone-len", "pre-line", "ring-line", "lvl-arg", "find-flag", "slot-cap")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "DRLG record, teardown and list helper", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
