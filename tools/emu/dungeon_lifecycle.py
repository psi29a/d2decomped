# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the room1 near-set, node-list, teardown and bit-table functions of DUNGEON\\Dungeon.cpp in stock game.exe 1.14d.

    uv run python dungeon_lifecycle.py [--mutation-check]

Runs game.exe's own code for 13 functions against a hand-written model on
random room1 / dungeon records (_dungeon_rig.py): return value, ordered callee
log, the whole arena, the blocks the arena allocator handed out, the
fatal-error line and the bytes `ret n` pops.

  FUN_0061a270 / FUN_0061a2c0 push a node on / free a
  room1's node list (+0x18); FUN_0061a460 all-near-rooms-flag test;
  FUN_0061a550 / FUN_0061a5a0 / FUN_0061a700 / FUN_0061a790 / FUN_0061a7e0 the
  sorted pointer set at +0x48 (count +0x78) and idle counter +0xc;
  FUN_0061a840 free a room1; FUN_0061a910 unlink a room1 from its dungeon and
  its neighbours; FUN_0061afd0 free a dungeon; FUN_0061ad70 / FUN_0061ae30 the
  bit-mask <-> table-entry conversions over 0x61_ddf0's table.

Hooked, logged, not claimed: the allocator and its free, 0x64_c450 (unit
removal; the stand-in unlinks the head unit), 0x64_ca10, 0x66_b4c0, 0x64_2190,
0x61_bec0, 0x66_bcf0, and 0x61_ddf0 (the table; its size is a case input).
The CRT memcpy runs for real. `--mutation-check` breaks the model five ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt

MUT = set()
UNIT_NEXT = 0xE8


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def put(img, slot, off, value):
    """Write a dword into slot `slot` of the arena image."""
    index = slot * 0x100 + off
    img[index:index + 4] = (value & M32).to_bytes(4, "little")


def pick(rng, address, null=0.25):
    """`address` or, sometimes, null."""
    return 0 if rng.random() < null else address


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig):
        """Hook the unclaimed callees and set the stand-ins."""
        self.rig = rig
        for name, addr, nstack, regs in (
                ("c64c450", 0x64_C450, 1, ()), ("c64ca10", 0x64_CA10, 0, ("ecx", "edx")),
                ("c66b4c0", 0x66_B4C0, 1, ("ecx", "edx")), ("c642190", 0x64_2190, 0, ("ecx",)),
                ("c61bec0", 0x61_BEC0, 0, ("ecx", "edx")), ("c66bcf0", 0x66_BCF0, 0, ("ecx",)),
                ("c61ddf0", 0x61_DDF0, 0, ())):
            rig.callee(name, addr, nstack, regs)
        rig.rooms, rig.n_val, rig.table_n = [], 0, 0
        rig.custom["c64c450"] = self.unlink_unit
        rig.custom["c66bcf0"] = lambda e: rig.n_val
        rig.custom["c61ddf0"] = self.table_hook

    def unlink_unit(self, e):
        """0x64_c450 stand-in: the unit leaves the head of whichever room's unit list holds it."""
        unit = e.arg(0)
        for room in self.rig.rooms:
            if e.r32(room + 0x74) == unit:
                e.w32(room + 0x74, e.r32(unit + UNIT_NEXT))
        return 0

    def table_hook(self, e):
        """0x61_ddf0 stand-in: write the case's entry count through ECX, return the table in slot 5."""
        from unicorn.x86_const import UC_X86_REG_ECX
        e.w32(e.mu.reg_read(UC_X86_REG_ECX), self.rig.table_n & M32)
        return self.rig.slot(5)

    # ---- builders --------------------------------------------------------------------
    def room(self, img, rng, slot, units, nodes):
        """A room1 in `slot` with a dungeon at slot 0, a unit chain over `units` and a node chain over `nodes`."""
        rig = self.rig
        put(img, slot, 0x2C, rig.slot(0))
        put(img, 0, 0x5C, rng.randrange(1 << 32))
        for i, u in enumerate(units):
            put(img, u, UNIT_NEXT, rig.slot(units[i + 1]) if i + 1 < len(units) else 0)
            flags = int.from_bytes(img[u * 0x100 + 0xC4:u * 0x100 + 0xC8], "little") & ~(1 << 22)
            put(img, u, 0xC4, flags)
        put(img, slot, 0x74, rig.slot(units[0]) if units else 0)
        for i, n in enumerate(nodes):
            put(img, n, 8, rig.slot(nodes[i + 1]) if i + 1 < len(nodes) else 0)
        put(img, slot, 0x18, rig.slot(nodes[0]) if nodes else 0)
        if rng.random() < 0.3:
            put(img, slot, 0x48, 0)

    # ---- the models ------------------------------------------------------------------
    def free_nodes(self, m, room):
        """FUN_0061a2c0 on the model: free each node of +0x18 with the room's dungeon pool, then clear the head."""
        node = m.r(room + 0x18)
        pool = m.r(m.r(room + 0x2C) + 0x5C)
        while node:
            nxt = m.r(node + 8)
            m.free(pool, node, 0x39C)
            node = nxt
        m.w(room + 0x18, 0)

    def free_room(self, m, pool, room):
        """FUN_0061a840 on the model (ECX pool, EDX room1)."""
        unit = m.r(room + 0x74)
        while unit:
            m.w(unit + 0xC8, m.r(unit + 0xC8) | 0x20)
            m.w(unit + 0xC4, m.r(unit + 0xC4) | 0x800000)
            m.call("c64c450", unit)
            m.w(room + 0x74, m.r(unit + UNIT_NEXT))
            unit = m.r(room + 0x74)
        m.call("c64ca10", pool, room)
        self.free_nodes(m, room)
        array = m.r(room + 0x48)
        if array:
            m.free(m.r(m.r(room + 0x2C) + 0x5C), array, 0x4E2)
        m.free(pool, m.r(room), 0x4E4)
        m.free(pool, room, 0x4E5)

    # ---- node list ---------------------------------------------------------------------
    def nodes(self):
        """FUN_0061a270, FUN_0061a2c0, FUN_0061a330 is in dungeon_accessors."""
        rig = self.rig

        def b_push(img, rng):
            """Room1 (sometimes null) whose dungeon is slot 0."""
            self.room(img, rng, 1, [], [12, 13][:rng.randrange(0, 3)])
            return {"stack": [pick(rng, rig.slot(1), 0.2), rng.randrange(1 << 32), rng.randrange(1 << 32)]}

        def m_push(m, a):
            """A 12-byte node {x, y, old head} becomes the head; the dungeon's +0x58 is set to 1."""
            r, x, y = a["stack"]
            if r == 0:
                return None
            dungeon = m.r(r + 0x2C)
            node = m.alloc(m.r(dungeon + 0x5C), 0xC, 0x384)
            m.w(node, x)
            m.w(node + 4, y)
            m.w(node + 8, m.r(r + 0x18))
            m.w(r + 0x18, node)
            m.w(dungeon + 0x58, 0 if mut("mark-clear") else 1)
        rig.run("FUN_0061a270", 0x61A270, b_push, m_push, 3, ret=False)

        def b_free(img, rng):
            """Room1 (sometimes null) with 0..3 nodes."""
            self.room(img, rng, 1, [], [12, 13, 14][:rng.randrange(0, 4)])
            return {"stack": [pick(rng, rig.slot(1), 0.15)]}

        def m_free(m, a):
            """Fatal 0x390 on null; else free every node (line 0x39c) and clear +0x18."""
            if a["stack"][0] == 0:
                raise Halt(0x390)
            self.free_nodes(m, a["stack"][0])
        rig.run("FUN_0061a2c0", 0x61A2C0, b_free, m_free, 1, ret=False)

    # ---- sorted set ----------------------------------------------------------------------
    def set_room(self, img, rng, slot=1, null=0.0, count=None, values=None):
        """A room1 whose +0x48 array (slot 6) holds `count` non-null (or rarely null) pointers; count at +0x78."""
        rig = self.rig
        count = rng.randrange(0, 7) if count is None else count
        put(img, slot, 0x48, rig.slot(6))
        put(img, slot, 0x78, count)
        for i in range(count):
            v = rng.choice((1, 5, 0x100, 0x7FFFFFFF, 0x80000000, 0xFFFFFFF0, rng.randrange(1, 1 << 32)))
            if values:
                v = rng.choice(values)
            put(img, 6, 4 * i, 0 if rng.random() < null else v)

    def sets(self):
        """FUN_0061a550, FUN_0061a5a0, FUN_0061a700, FUN_0061a790, FUN_0061a7e0, FUN_0061a460."""
        rig = self.rig

        def nonnull_scan(m, room):
            """FUN_0061a550 on the model: fatal 0x453 on a null entry among the first +0x78."""
            for i in range(m.r(room + 0x78)):
                if m.r(m.r(room + 0x48) + 4 * i) == 0:
                    raise Halt(0x453)
            return 1

        def b_scan(img, rng):
            """EDX room1 with a few entries, one may be null."""
            self.set_room(img, rng, null=0.12)
            return {"edx": rig.slot(1)}
        rig.run("FUN_0061a550", 0x61A550, b_scan, lambda m, a: nonnull_scan(m, a["edx"]), 0)

        def sort_model(m, room):
            """FUN_0061a5a0 on the model: scan, then sort the first +0x78 entries ascending (unsigned)."""
            nonnull_scan(m, room)
            n = m.r(room + 0x78)
            base = m.r(room + 0x48)
            values = sorted(m.r(base + 4 * i) for i in range(n))
            if mut("sort-signed"):
                values = sorted(values, key=lambda v: v - (1 << 32) if v >> 31 else v)
            for i, v in enumerate(values):
                m.w(base + 4 * i, v)

        def b_sort(img, rng):
            """EAX room1 with a few entries, one may be null."""
            self.set_room(img, rng, null=0.1)
            return {"regs": {"eax": rig.slot(1)}}
        rig.run("FUN_0061a5a0", 0x61A5A0, b_sort, lambda m, a: sort_model(m, a["regs"]["eax"]), 0, ret=False)

        def b_remove(img, rng):
            """Room1 (maybe null), a value that is usually in the set (sometimes null)."""
            self.set_room(img, rng, null=0.05, values=(2, 3, 9, 0x80000001, 0xFFFFFFFF))
            v = rng.choice((2, 3, 9, 0x80000001, 0xFFFFFFFF, 0, 4))
            return {"stack": [pick(rng, rig.slot(1), 0.1), v]}

        def m_remove(m, a):
            """Fatal 0x487 / 0x488 / 0x453 / 0x490; else swap the last entry into the hole, drop one, re-sort."""
            r, v = a["stack"]
            if r == 0:
                raise Halt(0x487)
            if v == 0:
                raise Halt(0x488)
            nonnull_scan(m, r)
            n = m.r(r + 0x78)
            base = m.r(r + 0x48)
            index = next((i for i in range(n) if m.r(base + 4 * i) == v), n)
            if index >= n:
                raise Halt(0x490)
            m.w(base + 4 * index, m.r(base + 4 * (n - 1)))
            m.w(r + 0x78, n - 1)
            sort_model(m, r)
        rig.run("FUN_0061a700", 0x61A700, b_remove, m_remove, 2, ret=False)

        def b_idle(img, rng):
            """Room1 whose set is empty or not, counter random."""
            self.set_room(img, rng, count=rng.choice((0, 1, 3)))
            put(img, 1, 0xC, rng.choice((0, 5, 0xFFFFFFFF, rng.randrange(1 << 32))))
            return {"stack": [pick(rng, rig.slot(1), 0.15)]}

        def m_idle(m, a):
            """Fatal 0x4ae on null; players present (+0x78 != 0) reset the idle counter, else it counts up."""
            r = a["stack"][0]
            if r == 0:
                raise Halt(0x4AE)
            if m.r(r + 0x78):
                m.w(r + 0xC, 0)
            else:
                m.w(r + 0xC, m.r(r + 0xC) + 1)
            return m.r(r + 0xC)
        rig.run("FUN_0061a790", 0x61A790, b_idle, m_idle, 1)

        def b_share(img, rng):
            """Two sets (slots 1 and 2) drawn from a small pool of values so subsets happen."""
            put(img, 2, 0x48, rig.slot(7))
            pool = (1, 2, 3, 4)
            self.set_room(img, rng, 1, count=rng.randrange(0, 4), values=pool)
            count = rng.randrange(0, 4)
            put(img, 2, 0x78, count)
            for i in range(count):
                put(img, 7, 4 * i, rng.choice(pool))
            return {"ecx": rig.slot(1), "edx": rig.slot(2)}

        def m_share(m, a):
            """1 when some entry of the EDX set is missing from the ECX set (also an empty ECX set), else 0."""
            mine = [m.r(m.r(a["ecx"] + 0x48) + 4 * i) for i in range(m.r(a["ecx"] + 0x78))]
            theirs = [m.r(m.r(a["edx"] + 0x48) + 4 * i) for i in range(m.r(a["edx"] + 0x78))]
            if mut("share-subset"):
                return int(all(v in mine for v in theirs) and bool(theirs))
            return int(any(v not in mine for v in theirs))
        rig.run("FUN_0061a7e0", 0x61A7E0, b_share, m_share, 0)

        def b_flags(img, rng):
            """Room1 (maybe null) with a near array of up to three room1s whose +0x34 bit 0 varies."""
            count = rng.randrange(0, 4)
            put(img, 1, 0x24, count)
            put(img, 1, 0, rig.slot(6))
            rig.n_val = count if rng.random() < 0.7 else count + 1
            for i in range(count):
                put(img, 6, 4 * i, rig.slot(8 + i))
                put(img, 8 + i, 0x34, rng.choice((1, 0, 3, 2, 0xFFFFFFFF)))
            return {"stack": [pick(rng, rig.slot(1), 0.1)]}

        def m_flags(m, a):
            """Fatal 0x3ef on null; 0 unless 0x66_bcf0 of room2 equals +0x24; then 1 when every near room has bit 0."""
            r = a["stack"][0]
            if r == 0:
                raise Halt(0x3EF)
            n = m.call("c66bcf0", m.r(r + 0x10))
            if m.r(r + 0x24) != n:
                return 0
            for i in range(n):
                if not m.r(m.r(m.r(r) + 4 * i) + 0x34) & 1:
                    return 0
            return 1
        rig.run("FUN_0061a460", 0x61A460, b_flags, m_flags, 1)

    # ---- teardown ------------------------------------------------------------------------------
    def teardown(self):
        """FUN_0061a840, FUN_0061a910, FUN_0061afd0."""
        rig = self.rig

        def b_free_room(img, rng):
            """Room1 in slot 1 with 0..3 units and 0..2 nodes."""
            self.room(img, rng, 1, [20, 21, 22][:rng.randrange(0, 4)], [12, 13][:rng.randrange(0, 3)])
            rig.rooms = [rig.slot(1)]
            return {"ecx": rng.randrange(1 << 32), "edx": rig.slot(1)}
        rig.run("FUN_0061a840", 0x61A840, b_free_room, lambda m, a: self.free_room(m, a["ecx"], a["edx"]), 0, ret=False)

        def b_unlink(img, rng):
            """Dungeon (slot 0) with a list of room1s around slot 1, whose near array lists other rooms."""
            order = [30, 1, 31]
            where = rng.choice(("head", "middle", "tail", "only", "absent", "empty"))
            chain = {"head": [1, 30, 31], "middle": [30, 1, 31], "tail": [30, 31, 1], "only": [1],
                     "absent": [30, 31], "empty": []}[where]
            put(img, 0, 0x10, rig.slot(chain[0]) if chain else 0)
            for i, s in enumerate(chain):
                put(img, s, 0x7C, rig.slot(chain[i + 1]) if i + 1 < len(chain) else 0)
            put(img, 0, 0x50, rng.randrange(1 << 32))
            self.room(img, rng, 1, [20, 21][:rng.randrange(0, 3)], [12][:rng.randrange(0, 2)])
            count = rng.randrange(0, 4)
            put(img, 1, 0x24, count)
            put(img, 1, 0, rig.slot(6))
            near = [1, 30, 31, 32]
            for i in range(count):
                put(img, 6, 4 * i, rig.slot(near[i]))
            for s in near[1:]:
                n = rng.randrange(0, 4)
                put(img, s, 0x24, n)
                put(img, s, 0, rig.slot(3 + s))
                for j in range(n):
                    put(img, 3 + s, 4 * j, rig.slot(rng.choice((1, 30, 31, 32, 36))))
            rig.rooms = [rig.slot(1)]
            del order
            return {"stack": [rig.slot(0), pick(rng, rig.slot(1), 0.1)]}

        def m_unlink(m, a):
            """Remove the room1 from the dungeon's list (fatal 0x502 if absent), drop it from each near room's own
            array (swap last in), call 0x66_b4c0, then free it with FUN_0061a840's steps."""
            d, room = a["stack"]
            if room == 0:
                raise Halt(0x4F7)
            node, prev = m.r(d + 0x10), 0
            while node and node != room:
                prev, node = node, m.r(node + 0x7C)
            if node == 0:
                raise Halt(0x502)
            if prev:
                m.w(prev + 0x7C, m.r(node + 0x7C))
            else:
                m.w(d + 0x10, m.r(node + 0x7C))
            for i in range(s_signed(m.r(room + 0x24))):
                n = m.r(m.r(room) + 4 * i)
                if n == room:
                    continue
                count = s_signed(m.r(n + 0x24))
                array = m.r(n)
                for j in range(max(count, 0)):
                    if m.r(array + 4 * j) == room:
                        m.w(array + 4 * j, m.r(array + 4 * (count - 1)))
                        m.w(n + 0x24, m.r(n + 0x24) - 1)
                        break
            m.call("c66b4c0", m.r(room + 0x10), m.r(d + 0x50), m.r(room + 0x34))
            self.free_room(m, m.r(d + 0x5C), room)
        rig.run("FUN_0061a910", 0x61A910, b_unlink, m_unlink, 2, ret=False)

        def b_free_dungeon(img, rng):
            """Dungeon (maybe null) with 0..2 rooms, an optional act pointer and an optional +4 block."""
            rooms = [30, 31][:rng.randrange(0, 3)]
            for i, s in enumerate(rooms):
                base = 20 + 4 * i
                self.room(img, rng, s, [base, base + 1][:rng.randrange(0, 3)], [12 + 2 * i][:rng.randrange(0, 2)])
                put(img, s, 0x7C, rig.slot(rooms[i + 1]) if i + 1 < len(rooms) else 0)
            put(img, 0, 0x10, rig.slot(rooms[0]) if rooms else 0)
            put(img, 0, 0x48, rng.choice((0, rng.randrange(1 << 32))))
            put(img, 0, 4, rng.choice((0, rng.randrange(1 << 32))))
            rig.rooms = [rig.slot(s) for s in rooms]
            return {"stack": [pick(rng, rig.slot(0), 0.1)]}

        def m_free_dungeon(m, a):
            """Fatal 0x3a on null; free the act state, each room1 (next link read first), the +4 block, the dungeon."""
            d = a["stack"][0]
            if d == 0:
                raise Halt(0x3A)
            if m.r(d + 0x48):
                m.call("c642190", m.r(d + 0x48))
                m.w(d + 0x48, 0)
            room = m.r(d + 0x10)
            while room:
                nxt = m.r(room + 0x7C)
                self.free_room(m, m.r(d + 0x5C), room)
                room = nxt
            if m.r(d + 4):
                m.call("c61bec0", m.r(d + 0x5C), m.r(d + 4))
                m.w(d + 4, 0)
            m.free(m.r(d + 0x5C), d, 0x52)
        rig.run("FUN_0061afd0", 0x61AFD0, b_free_dungeon, m_free_dungeon, 1, ret=False)

    # ---- bit table ----------------------------------------------------------------------------------
    def bits(self):
        """FUN_0061ad70 and FUN_0061ae30."""
        rig = self.rig

        def b_select(img, rng):
            """Pool, a mask, two out pointers (slots 3 and 4); the table has 0..33 entries."""
            rig.table_n = rng.choice((0, 1, 5, 12, 32, 32, 33, rng.randrange(0, 34)))
            return {"stack": [rng.randrange(1 << 32), rng.choice((0, M32, 0x55555555, rng.randrange(1 << 32))),
                              rig.slot(3), rig.slot(4)]}

        def m_select(m, a):
            """Fatal 0x60a when the table is longer than 32; else copy the table entries whose bit is set into a new block."""
            pool, mask, out_ptr, out_count = a["stack"]
            n = rig.table_n
            m.call("c61ddf0")
            if n > 0x20:
                raise Halt(0x60A)
            table = rig.slot(5)
            chosen = [m.r(table + 4 * i) for i in range(n) if (mask >> i) & 1 and not (mut("bit-off-by-one") and i == 0)]
            if chosen:
                block = m.alloc(pool, 4 * len(chosen), 0x614)
                for i, v in enumerate(chosen):
                    m.w(block + 4 * i, v)
                m.w(out_ptr, block)
            else:
                m.w(out_ptr, 0)
            m.w(out_count, len(chosen))
        rig.run("FUN_0061ad70", 0x61AD70, b_select, m_select, 4, ret=False)

        def b_bit(img, rng):
            """A value that is often a table entry; table of 0..32 entries."""
            rig.table_n = rng.choice((0, 3, 12, 32, rng.randrange(0, 33)))
            entry = int.from_bytes(img[5 * 0x100 + 4 * rng.randrange(0, 32):][:4], "little")
            return {"stack": [rng.choice((entry, entry, rng.randrange(1 << 32)))]}

        def m_bit(m, a):
            """1 << index of the first table entry equal to the value, 0 if none."""
            m.call("c61ddf0")
            for i in range(rig.table_n):
                if m.r(rig.slot(5) + 4 * i) == a["stack"][0]:
                    return (1 << i) & M32
            return 0
        rig.run("FUN_0061ae30", 0x61AE30, b_bit, m_bit, 1)


def s_signed(v):
    """Signed view of a 32-bit value."""
    return rig_mod.s32(v)


def suite(rig):
    """Run every group; returns the number of cases."""
    s = Suite(rig)
    s.nodes()
    s.sets()
    s.teardown()
    s.bits()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("mark-clear", "sort-signed", "share-subset", "bit-off-by-one")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "Dungeon.cpp room1 node, set, teardown and bit-table", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
