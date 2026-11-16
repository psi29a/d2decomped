# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 24 more small Items.cpp functions in stock game.exe 1.14d.

    uv run python items_traces3.py [--mutation-check | --coverage]

The third set after items_traces.py and items_traces2.py, on the same rig
(_dungeon_rig.py). Runs game.exe's own code for the Items.cpp functions
FUN_00627df0 (socket count), 0062a8d0 (ethereal), 0062a0f0 (quality 4..9),
0062a2f0 (plain quality, EAX), 006288d0 (rangeadder), 00629900 (hasinv),
006299b0 (stat 0xc2), 0062a1e0 (a Barbarian's one-or-two-hander),
0062a270 (holds anything, EAX), 00629c80 / 00629cc0 (bitfield1 bits 0 and 1),
00629d40 (sellable), 00629f70 (type Class below 7), 00628480 (ItemTypes
+0xe by index), 0062ba80, 0062e6f0, 0062e740, 0062e790, 0062e880 (the
item's ItemTypes Throwable, Shoots, Quiver, AutoStack, StorePage),
0062a5d0 / 0062a620 (life and mana potions by class), 00558610 (the
diminishing magic-find curve) and 00558450 / 005584c0 (a health or mana
potion code by act) against hand-written models on random unit, item-data
and table bytes: the whole EAX, the ordered callee log with register and
stack arguments, the arena, the fatal-error line and the bytes `ret n`
pops. Every callee is hooked and logged, not claimed; the two potion code
tables (0x731f4c, 0x731f60) and their counts are randomised in place.
`--mutation-check` breaks the model 21 ways; `--coverage` lists any
instruction of the 24 bodies the cases never reach (none, apart from the
fatal-exit tails).
"""
# gaps-runs: 00627df0 0062a8d0 0062a0f0 0062a2f0 006288d0 00629900 006299b0 0062a1e0 0062a270 00629c80
# gaps-runs: 00629cc0 00629d40 00629f70 00628480 0062ba80 0062e6f0 0062e740 0062e790 0062e880 0062a5d0
# gaps-runs: 0062a620 00558610 00558450 005584c0
import argparse
import sys
from pathlib import Path

import capstone
from unicorn import UC_HOOK_CODE

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt, s32

MUT = set()
DATA = 0x400            # item data (unit + 0x14)
RECORDS = 0x800         # Items rows, 0x300 apart, classes 0..3
PLAYER = 0x1480         # a second unit: a player (type +0, class +4, inventory +0x60)
LEVEL = 0x1600          # FUN_00554010's answer (byte +0x6d)
TABLE = 0x2800          # the stand-in sgptDataTable (global 0x744304)
TYPES = 0x3500          # ItemTypes rows, 0xe4 apart
HP_CODES, MP_CODES = 0x73_1F4C, 0x73_1F60   # four potion codes each, then the count
CODES_SIZE = 0x28       # both tables: 2 x (4 codes + count)


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def put(img, off, value, size=4):
    """Write a little-endian value at arena offset `off`."""
    img[off:off + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little")


def trunc_div(num, den):
    """idiv: signed 32-bit quotient rounded toward zero."""
    q = abs(num) // abs(den)
    return q if (num < 0) == (den < 0) else -q


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig):
        """Hook the callees and point the data-table global at the arena."""
        self.rig = rig
        self.pick = {}          # per-case answers the custom hooks give (set by the builders)
        rig.m.w32(0x744304, rig.arena + TABLE)
        rnd = rig.rng
        for name, addr, nstack, regs in (
                ("c6335f0", 0x63_35F0, 1, ()), ("c62b400", 0x62_B400, 1, ()), ("c625480", 0x62_5480, 3, ()),
                ("c63b2c0", 0x63_B2C0, 1, ()), ("c63dfd0", 0x63_DFD0, 1, ()), ("c63dfa0", 0x63_DFA0, 1, ()),
                ("c620bb0", 0x62_0BB0, 1, ()), ("c61a1b0", 0x61_A1B0, 1, ()), ("c6427f0", 0x64_27F0, 1, ()),
                ("c554010", 0x55_4010, 0, ("ecx",))):
            rig.callee(name, addr, nstack, regs)
        at = self.at

        def row(e):
            """FUN_006335f0 stand-in: the arena's Items row for classes 0..3, else none."""
            return at(RECORDS) + 0x300 * e.arg(0) if e.arg(0) < 4 else 0

        def walk(e, key):
            """The inventory walk stand-ins answer from the case's node list."""
            return self.pick[key].get(e.arg(0), 0)

        custom = rig.custom
        custom["c6335f0"] = row
        custom["c62b400"] = lambda e: self.pick.get("type", 0)
        custom["c63b2c0"] = lambda e: self.pick["first"]
        custom["c63dfa0"] = lambda e: walk(e, "next")
        custom["c63dfd0"] = lambda e: walk(e, "item")
        custom["c620bb0"] = lambda e: rnd.choice((0, 1, rnd.getrandbits(32)))
        custom["c6427f0"] = lambda e: (rnd.getrandbits(24) << 8) | rnd.choice((0, 1, 2, 3, 4, 0xFE, 0xFF,
                                                                               rnd.getrandbits(8)))
        custom["c554010"] = lambda e: rnd.choice((0, at(LEVEL)))

    def at(self, off):
        """Arena address of offset `off`."""
        return self.rig.arena + off

    def unit(self, img, rng, cls=None, kind=0.8, data=0.85, null=0.08):
        """Fill the item at offset 0 (type, class, data pointer) and return it, or null."""
        put(img, 0, 4 if rng.random() < kind else rng.choice((0, 1, 3, 5, 0x10004)))
        put(img, 4, cls if cls is not None else rng.choice((0, 1, 2, 3, 3, 4, 0xFFFFFFFF)))
        put(img, 0x14, self.at(DATA) if rng.random() < data else 0)
        return 0 if rng.random() < null else self.at(0)

    @staticmethod
    def is_item(m, u):
        """The valid-item guard: nonnull, type 4."""
        return u != 0 and m.r(u) == (3 if mut("type3") else 4)

    def data(self, m, u):
        """D of a valid item, else 0."""
        return m.r(u + 0x14) if self.is_item(m, u) else 0

    def b_unit(self, img, rng):
        """One item argument."""
        return {"stack": [self.unit(img, rng)]}

    def item_data(self):
        """Getters on the item's data block."""
        rig = self.rig

        def m_sockets(m, a):
            """FUN_00627df0: dword D + 0x10 for a valid item with data, else 0."""
            d = self.data(m, a["stack"][0])
            return m.r(d + (0x10 if not mut("sock-off") else 0xC)) if d else 0
        rig.run("FUN_00627df0", 0x627DF0, self.b_unit, m_sockets, 1)

        def m_ethereal(m, a):
            """FUN_0062a8d0: D + 0x18 & 0x400000 for a valid item with data, else 0."""
            d = self.data(m, a["stack"][0])
            return m.r(d + 0x18) & (0x400000 if not mut("eth-bit") else 0x40000) if d else 0
        rig.run("FUN_0062a8d0", 0x62A8D0, self.b_unit, m_ethereal, 1)

        def b_quality(img, rng):
            """An item whose quality (D + 0) is around 4..9."""
            put(img, DATA, rng.choice((0, 1, 3, 4, 5, 8, 9, 10, 0x80000004, 0xFFFFFFFF, rng.getrandbits(32))))
            return self.b_unit(img, rng)

        def m_magic(m, a):
            """FUN_0062a0f0: 1 when a valid item with data has signed quality 4..9, else 0."""
            d = self.data(m, a["stack"][0])
            if not d:
                return 0
            q = s32(m.r(d))
            return int(4 <= q <= (9 if not mut("quality-10") else 10))
        rig.run("FUN_0062a0f0", 0x62A0F0, b_quality, m_magic, 1)

        def b_plain(img, rng):
            """A quality in EAX."""
            return {"regs": {"eax": rng.choice((0, 1, 3, 4, 9, 10, 0xFFFFFFFF, 0x80000000, rng.getrandbits(32)))}}

        def m_plain(m, a):
            """FUN_0062a2f0: 0 for quality 0, else 1 when signed at most 3 or above 9."""
            q = s32(a["regs"]["eax"])
            return int(q != 0 and (q <= (3 if not mut("plain-4") else 4) or q > 9))
        rig.run("FUN_0062a2f0", 0x62A2F0, b_plain, m_plain, 0)

    def items_rows(self):
        """Items-row getters: through the item's class, or by class id."""
        rig = self.rig

        def b_ranged(img, rng):
            """An item of class 0..3 (game.exe reads the row unchecked)."""
            return {"stack": [self.unit(img, rng, cls=rng.randrange(4))]}

        def m_range(m, a):
            """FUN_006288d0: AL = the row's byte + 0x104 for a valid item (upper EAX the row's), else AL = 0 over the unit."""
            u = a["stack"][0]
            if not self.is_item(m, u):
                return u & ~0xFF & M32
            rec = m.call("c6335f0", m.r(u + 4))
            return (rec & ~0xFF & M32) | m.r(rec + (0x104 if not mut("range-off") else 0x105), 1)
        rig.run("FUN_006288d0", 0x6288D0, b_ranged, m_range, 1)

        def m_hasinv(m, a):
            """FUN_00629900: the row's byte + 0x137 for a valid item with a row, else 0."""
            u = a["stack"][0]
            if not self.is_item(m, u):
                return 0
            rec = m.call("c6335f0", m.r(u + 4))
            return m.r(rec + 0x137, 1) if rec else 0
        rig.run("FUN_00629900", 0x629900, self.b_unit, m_hasinv, 1)

        def m_numsockets(m, a):
            """FUN_006299b0: fatal 0xea6 / 0xea7, else FUN_00625480(item, 0xc2, 0)."""
            u = a["stack"][0]
            if u == 0:
                raise Halt(0xEA6)
            if m.r(u) != 4:
                raise Halt(0xEA7)
            return m.call("c625480", u, 0xC2 if not mut("stat-c3") else 0xC3, 0)
        rig.run("FUN_006299b0", 0x6299B0, self.b_unit, m_numsockets, 1)

        def b_barb(img, rng):
            """A wielder (type 0 class 4 mostly) and an item; either may be null."""
            put(img, PLAYER, rng.choice((0, 0, 0, 1, 4)))
            put(img, PLAYER + 4, rng.choice((4, 4, 0, 3, 5, 0x104)))
            for k in range(4):
                img[RECORDS + 0x300 * k + 0x13D] = rng.choice((0, 0, 1, rng.randrange(256)))
            wielder = rng.choice((self.at(PLAYER),) * 7 + (0,))
            return {"stack": [wielder, self.unit(img, rng, null=0.1)]}

        def m_barb(m, a):
            """FUN_0062a1e0: fatal 0x119b (no item) / 0x119d (no wielder); a Barbarian player's row byte + 0x13d != 0 (0x11a5 without a row)."""
            who, item = a["stack"]
            if item == 0:
                raise Halt(0x119B)
            if who == 0:
                raise Halt(0x119D)
            if m.r(who) != 0 or m.r(who + 4) != (4 if not mut("barb-3") else 3):
                return 0
            rec = m.call("c6335f0", m.r(item + 4))
            if rec == 0:
                raise Halt(0x11A5)
            return int(m.r(rec + 0x13D, 1) != 0)
        rig.run("FUN_0062a1e0", 0x62A1E0, b_barb, m_barb, 2)

        def b_holds(img, rng):
            """A player in EAX; a node list of 0..4 nodes, some holding an item."""
            put(img, PLAYER + 0x60, rng.getrandbits(32))
            nodes = [rng.getrandbits(32) | 1 for _ in range(rng.choice((0, 1, 2, 4)))]
            self.pick["first"] = nodes[0] if nodes else 0
            self.pick["next"] = {n: (nodes[k + 1] if k + 1 < len(nodes) else 0) for k, n in enumerate(nodes)}
            self.pick["item"] = {n: rng.choice((0, 0, 0, rng.getrandbits(32))) for n in nodes}
            return {"regs": {"eax": self.at(PLAYER)}}

        def m_holds(m, a):
            """FUN_0062a270: 1 when a node of the player's inventory (unit + 0x60) holds an item, else 0."""
            node = m.call("c63b2c0", m.r(a["regs"]["eax"] + 0x60))
            while node:
                if m.call("c63dfd0", node):
                    return 1 if not mut("holds-0") else 0
                node = m.call("c63dfa0", node)
            return 0
        rig.run("FUN_0062a270", 0x62A270, b_holds, m_holds, 0)

        def b_cls(img, rng):
            """A class id: rows 0..3, a missing row, a wild id."""
            return {"stack": [rng.choice((0, 1, 2, 3, 4, 0xFFFFFFFF, rng.getrandbits(32)))]}

        def by_cls(line, field):
            """Model of the class getters: the row or fatal `line`, then field(m, row)."""
            def model(m, a):
                """The row lookup is hooked."""
                rec = m.call("c6335f0", a["stack"][0])
                if rec == 0:
                    raise Halt(line)
                return field(m, rec)
            return model
        rig.run("FUN_00629c80", 0x629C80, b_cls, by_cls(0x1040, lambda m, r: m.r(r + 0xDC) & 1), 1)
        rig.run("FUN_00629cc0", 0x629CC0, b_cls,
                by_cls(0x104A, lambda m, r: m.r(r + 0xDC) & (2 if not mut("bit-1") else 1)), 1)

        def b_sell(img, rng):
            """b_cls with each row's quest byte (+0x12a) and type (+0x11e) around 0 and 0x27."""
            for k in range(4):
                img[RECORDS + 0x300 * k + 0x12A] = rng.choice((0, 0, 1, 2))
                put(img, RECORDS + 0x300 * k + 0x11E, rng.choice((0x27, 0x27, 0x26, 0x10027, rng.getrandbits(16))), 2)
            return b_cls(img, rng)

        def sellable(m, r):
            """Quest byte 0 and type word not 0x27."""
            return int(m.r(r + 0x12A, 1) == 0 and m.r(r + 0x11E, 2 if not mut("type-dword") else 4) != 0x27)
        rig.run("FUN_00629d40", 0x629D40, b_sell, by_cls(0x105E, sellable), 1)

        def b_class(img, rng):
            """b_cls with each row's type (signed word + 0x11e) around the ItemTypes table and the rows' Class bytes."""
            _, null = self.types(img, rng)
            for k in range(4):
                put(img, RECORDS + 0x300 * k + 0x11E, 0 if null else rng.choice((0, 1, 2, 3, 4, 0xFFFF, 0x8000)), 2)
            return b_cls(img, rng)

        def classed(m, r):
            """The type's row byte + 0x21 below 7 (unsigned); 0 without a row."""
            t = self.type_at(m, s32(m.r(r + 0x11E, 2) << 16) >> 16)
            return int(m.r(t + 0x21, 1) < (7 if not mut("class-8") else 8)) if t else 0
        rig.run("FUN_00629f70", 0x629F70, b_class, by_cls(0x68D, classed), 1)

    def types(self, img, rng):
        """Fill the ItemTypes count (0..4) and rows pointer (null 15% of the time); return the count and whether it's null.

        game.exe tests only rows + index * 0xe4 for null, so a null table is read at index 0 alone.
        """
        count = rng.choice((0, 1, 2, 3, 4))
        null = rng.random() < 0.15
        put(img, TABLE + 0xBFC, count)
        put(img, TABLE + 0xBF8, 0 if null else self.at(TYPES))
        for k in range(4):
            if rng.random() < 0.5:
                img[TYPES + 0xE4 * k + 0x21] = rng.choice((0, 6, 7, 8, 0xFF))
                img[TYPES + 0xE4 * k + 0x22] = rng.choice((0, 3, 0xFF, 0xFF))
        return count, null

    def type_at(self, m, idx):
        """ItemTypes row of signed index `idx`, or 0: in [0, count) and rows + idx * 0xe4 nonzero."""
        tbl = self.at(TABLE)
        if idx < 0 or idx >= s32(m.r(tbl + 0xBFC)) + (1 if mut("type-count") else 0):
            return 0
        return (m.r(tbl + 0xBF8) + idx * 0xE4) & M32

    def type_rows(self):
        """ItemTypes row getters: by index, and through the item's type."""
        rig = self.rig

        def b_index(img, rng):
            """A type index around the table."""
            count, null = self.types(img, rng)
            idx = 0 if null else rng.choice((0, 1, 2, 3, count, count - 1, -1, 0x80000000, 0x7FFFFFFF))
            return {"stack": [idx & M32]}

        def m_quiver(m, a):
            """FUN_00628480: the row's signed word + 0xe, 0 without a row."""
            t = self.type_at(m, s32(a["stack"][0]))
            return s32(m.r(t + 0xE, 2) << 16) >> 16 if t else 0
        rig.run("FUN_00628480", 0x628480, b_index, m_quiver, 1)

        def b_type(img, rng):
            """An item and the type index FUN_0062b400 gives for it."""
            count, null = self.types(img, rng)
            idx = 0 if null else rng.choice((0, 1, 2, 3, count, count - 1, count + 1, -1, 0x80000000, 0x7FFFFFFF))
            self.pick["type"] = idx & M32
            return {"stack": [self.unit(img, rng)]}

        def item_type(m, u):
            """The item's ItemTypes row, or 0 (not an item, or no row)."""
            return self.type_at(m, s32(m.call("c62b400", u))) if self.is_item(m, u) else 0

        def field(off, signed_word=False):
            """A zero-extended byte or a sign-extended word of the item's row; 0 without one."""
            def model(m, a):
                """ItemTypes getter."""
                t = item_type(m, a["stack"][0])
                if not t:
                    return 0
                return s32(m.r(t + off, 2) << 16) >> 16 if signed_word else m.r(t + off, 1)
            return model
        rig.run("FUN_0062ba80", 0x62BA80, b_type, field(0x10 if not mut("throw-off") else 0x11), 1)
        rig.run("FUN_0062e6f0", 0x62E6F0, b_type, field(0xC, True), 1)
        rig.run("FUN_0062e740", 0x62E740, b_type, field(0xE, not mut("quiver-unsigned")), 1)
        rig.run("FUN_0062e790", 0x62E790, b_type, field(0x13), 1)

        def m_page(m, a):
            """FUN_0062e880: AL = the row's byte + 0x22 over the row's upper bits; without a row (or byte 0xff) AL |= 0xff over what EAX held."""
            u = a["stack"][0]
            if not self.is_item(m, u):
                return (u & ~0xFF & M32) | 0xFF if u else 0xFF
            idx = m.call("c62b400", u)
            t = self.type_at(m, s32(idx))
            if not t:
                held = idx if s32(idx) < 0 or s32(idx) >= s32(m.r(self.at(TABLE) + 0xBFC)) else 0
                return (held & ~0xFF & M32) | 0xFF
            return (t & ~0xFF & M32) | (m.r(t + 0x22, 1) if not mut("page-ff-0") else 0)
        rig.run("FUN_0062e880", 0x62E880, b_type, m_page, 1)

    def potions(self):
        """Potion amounts by class, the magic-find curve and the potion codes by act."""
        rig, at = self.rig, self.at

        def b_drinker(img, rng):
            """A drinker (a player of class 0..7, another unit type, or null) and an amount."""
            put(img, PLAYER, rng.choice((0, 0, 0, 1, 3)))
            put(img, PLAYER + 4, rng.choice((0, 1, 2, 3, 4, 5, 6, 7, 0xFFFFFFFF)))
            amount = rng.choice((0, 1, 7, 100, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, rng.getrandbits(32)))
            return {"stack": [rng.choice((at(PLAYER),) * 6 + (0,)), amount]}

        def bonus(life):
            """FUN_0062a5d0 (life) / FUN_0062a620 (mana): x1.5 (v + v sar 1), x2 or x1 by class; non-players life x2, mana x1."""
            half = (0, 3, 6)
            double = (4,) if life else (1, 2, 5)
            if mut("half-asn"):
                half = (0, 3)

            def model(m, a):
                """Players of class 0..6 only; 32-bit wraps."""
                who, v = a["stack"]
                if who == 0 or m.r(who) != 0:
                    return (2 * v if life else v) & M32
                cls = m.r(who + 4)
                if cls in half:
                    return (v + (s32(v) >> 1)) & M32
                if cls in double:
                    return 2 * v & M32
                return v
            return model
        rig.run("FUN_0062a5d0", 0x62A5D0, b_drinker, bonus(True), 2)
        rig.run("FUN_0062a620", 0x62A620, b_drinker, bonus(False), 2)

        def b_curve(img, rng):
            """Magic find in ECX, the curve's k in EDX, the base odds on the stack: play values and the 32-bit range."""
            while True:
                if rng.random() < 0.5:
                    mf = rng.choice((0, 1, 50, 0x6E, 0x6F, 100, 101, 300, 1000))
                    k, c = rng.choice((250, 500, 600)), rng.randrange(0, 2000)
                else:
                    mf, k, c = (rng.choice((rng.getrandbits(32), rng.getrandbits(8), 0x7FFFFFFF, 0x80000000))
                                for _ in range(3))
                den = s32((mf - 100) + k)
                num = s32(s32(mf - 100) * s32(c))
                if s32(mf) <= 0x6E or (den != 0 and not (num == -0x80000000 and den == -1)):
                    return {"ecx": mf, "edx": k, "stack": [c]}

        def m_curve(m, a):
            """FUN_00558610: mf when signed at most 110, else ((mf - 100) * c) / ((mf - 100) + k) + 100 (32-bit imul, idiv)."""
            mf, k, c = a["ecx"], a["edx"], a["stack"][0]
            if s32(mf) <= (0x6E if not mut("curve-6f") else 0x6F):
                return mf
            less = s32(mf - 100)
            return trunc_div(s32(less * s32(c)), s32(less + k)) + 100
        rig.run("FUN_00558610", 0x558610, b_curve, m_curve, 1)

        def b_code(img, rng):
            """A unit in ECX; the two code tables and counts randomised in game.exe's data."""
            codes = bytearray(rng.randbytes(CODES_SIZE))
            put(codes, 0x10, rng.choice((0, 1, 3, 4, 5, 0x100, 0xFFFFFFFF)))
            put(codes, 0x24, rng.choice((0, 1, 3, 4, 5, 0x100, 0xFFFFFFFF)))
            rig.m.mu.mem_write(HP_CODES, bytes(codes))
            img[LEVEL + 0x6D] = rng.choice((0, 0, 1, 2, 0xFF))
            return {"ecx": rng.getrandbits(32)}

        def code(table, line):
            """FUN_00558450 (health) / FUN_005584c0 (mana): fatal `line` without FUN_00620bb0's answer; byte FUN_006427f0(FUN_0061a1b0(it)), + 2 when FUN_00554010's byte + 0x6d is set; its code, else the table's last."""
            def model(m, a):
                """The table and count are read from game.exe's memory."""
                unit = a["ecx"]
                r = m.call("c620bb0", unit)
                if r == 0:
                    raise Halt(line)
                n = m.call("c6427f0", m.call("c61a1b0", r)) & 0xFF
                x = m.call("c554010", unit)
                if x and m.r(x + 0x6D, 1) > 0:
                    n = (n + (2 if not mut("code-plus3") else 3)) & 0xFF
                if n < rig.m.r32(table + 0x10) if not mut("code-signed") else s32(n) < s32(rig.m.r32(table + 0x10)):
                    return rig.m.r32(table + 4 * n)
                return rig.m.r32(table + 0xC)
            return model
        stock = rig.m.read(HP_CODES, CODES_SIZE)
        try:
            rig.run("FUN_00558450", 0x558450, b_code, code(HP_CODES, 0xD5E), 0)
            rig.run("FUN_005584c0", 0x5584C0, b_code, code(MP_CODES if not mut("mp-hp") else HP_CODES, 0xD7D), 0)
        finally:
            rig.m.mu.mem_write(HP_CODES, stock)


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 80
    s = Suite(rig)
    s.item_data()
    s.items_rows()
    s.type_rows()
    s.potions()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("type3", "sock-off", "eth-bit", "quality-10", "plain-4", "range-off", "stat-c3", "barb-3",
                 "holds-0", "bit-1", "type-dword", "class-8", "type-count", "throw-off", "quiver-unsigned",
                 "page-ff-0", "half-asn", "curve-6f", "code-plus3", "code-signed", "mp-hp")
}


FATAL_TAIL = {"add esp, 0xc", "push -1", "call 0x681e09"}   # after the halt hook stops the run


def coverage():
    """Run the suite with a code hook over each declared body; print the instructions that never ran."""
    targets = [int(a, 16) for line in Path(__file__).read_text(encoding="utf-8").splitlines()
               if line.startswith("# gaps-runs:") for a in line.partition(":")[2].split()]
    table = Path(__file__).resolve().parents[2] / "docs/research/re/game-functions.tsv"
    sizes = {int(row.split("\t")[0], 16): int(row.split("\t")[2])
             for row in table.read_text(encoding="utf-8").splitlines()[1:]}
    rig = rig_mod.Rig()
    seen = set()
    for start in targets:
        rig.m.mu.hook_add(UC_HOOK_CODE, lambda mu, addr, size, _: seen.add(addr), None, start, start + sizes[start] - 1)
    suite(rig)
    disasm = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    missed = 0
    for start in targets:
        for ins in disasm.disasm(rig.m.read(start, sizes[start]), start):
            text = f"{ins.mnemonic} {ins.op_str}".strip()
            if ins.address not in seen and text not in FATAL_TAIL:
                print(f"{start:08x}: never ran {ins.address:08x} {text}")
                missed += 1
    print(f"coverage: {len(targets)} functions, {missed} instructions never ran (fatal-exit tails not counted)")


def main():
    """Parse the flags and run the suite, the mutation check or the coverage report."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    parser.add_argument("--coverage", action="store_true")
    args = parser.parse_args()
    if args.coverage:
        coverage()
        return
    rig_mod.run_suite(suite, MUTATIONS, "ITEMS small function (third set)", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
