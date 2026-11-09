# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 39 small ITEMS functions (Items.cpp, ItemMode.cpp) in stock game.exe 1.14d.

    uv run python items_traces.py [--mutation-check]

Runs game.exe's own code for FUN_0055d930, 0055e9a0, 0062a360 (stubs),
FUN_0055c460, 0055fe60, 0055fde0, 0055fe00, 0055d940, 0055f2a0, 0055bf30,
0055c4d0, 0055ca00, 0062ba60, 0062e410, 00628950, 0062a250, 0062af50 (wrappers
and pure helpers), FUN_006281b0, 006282a0, 006283f0, 006282d0, 00629ea0,
00627dc0, 006288a0, 00629860, 006298a0, 00628440, 006284c0, 006298e0, 00629990
(accessors), FUN_00629620, 006296b0, 00628980, 00629a00, 00629d00, 0062a2b0
(fatal class-id getters) and FUN_00627d00, 0062a900, 0062a490 against
hand-written models on random unit, item-data and Items-record bytes
(_dungeon_rig.py): the whole EAX (including the upper bits a narrow return
leaves behind), the ordered callee log with register and stack arguments, the
arena, the fatal-error line and the bytes `ret n` pops. FUN_0055f240 (body slot
pairing), FUN_00650e30 (seed reset) run for real. Hooked, logged, not claimed:
FUN_006335f0 (Items row), FUN_0055c2c0, FUN_0055dbc0, FUN_00621000, FUN_006280d0,
FUN_0062ff70, FUN_0055c730, FUN_0063d2b0, FUN_00621190, FUN_0063bde0,
FUN_0055bed0, FUN_00647960, FUN_0062b5b0, FUN_0062ae20, FUN_00628900,
FUN_00410eb0, FUN_0063aaf0, FUN_0063b2c0, FUN_0063dfa0, FUN_00629da0 and the
allocator free. Not exercised: FUN_006288a0 on a class with no Items row (the
stock code reads the null row and the rig cannot map it).
`--mutation-check` breaks the model 20 ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt, s32

MUT = set()
TABLE = 0x2800          # arena offset of the stand-in sgptDataTable (global 0x744304)
RECORDS = 0x800         # Items rows, 0x300 apart, classes 0..3
DATA = 0x400            # item data (unit + 0x14)
INV = 0x1800            # item inventory
NODES = 0x2000          # inventory node chain, 0x40 apart, next at +0x10
TYPES = 0x3500          # item type rows, 0xe4 apart
ITEMS_CPP = 0x6E_1150   # ".\\ITEMS\\Items.cpp" literal FUN_00627d00 passes to the free


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def put(img, off, value):
    """Write a dword at arena offset `off`."""
    img[off:off + 4] = (value & M32).to_bytes(4, "little")


def is_item(m, u):
    """The valid-item guard shared by the accessors: nonnull unit of type 4."""
    return u != 0 and m.r(u) == (3 if mut("type3") else 4)


def residue(u, low):
    """EAX when a narrow getter leaves through `xor al, al` / `or al, 0xff`: the pointer's upper 24 bits, then `low`."""
    return (u & ~0xFF & M32) | low


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig):
        """Hook the callees this oracle leaves unclaimed, point the data-table global at the arena."""
        self.rig = rig
        rig.m.w32(0x744304, rig.arena + TABLE)
        rnd = rig.rng
        at = self.at
        for name, addr, nstack, regs in (
                ("c6335f0", 0x63_35F0, 1, ()), ("c55c2c0", 0x55_C2C0, 2, ("ecx", "edx")),
                ("c55dbc0", 0x55_DBC0, 1, ("ecx", "edx")), ("c621000", 0x62_1000, 2, ()),
                ("c6280d0", 0x62_80D0, 3, ()), ("c62ff70", 0x62_FF70, 2, ()),
                ("c55c730", 0x55_C730, 3, ("ecx", "edx")), ("c63d2b0", 0x63_D2B0, 2, ()),
                ("c621190", 0x62_1190, 1, ()), ("c63bde0", 0x63_BDE0, 2, ()),
                ("c55bed0", 0x55_BED0, 0, ("ecx", "edx")), ("c647960", 0x64_7960, 2, ()),
                ("c62b5b0", 0x62_B5B0, 0, ("ecx", "edx")), ("c62ae20", 0x62_AE20, 5, ()),
                ("c628900", 0x62_8900, 2, ()), ("c410eb0", 0x41_0EB0, 3, ()),
                ("c63aaf0", 0x63_AAF0, 0, ("ecx",)), ("c63b2c0", 0x63_B2C0, 1, ()),
                ("c63dfa0", 0x63_DFA0, 1, ()), ("c629da0", 0x62_9DA0, 1, ())):
            rig.callee(name, addr, nstack, regs)
        rig.custom["c6335f0"] = lambda e: at(RECORDS) + 0x300 * e.arg(0) if e.arg(0) < 4 else 0
        rig.custom["c647960"] = lambda e: rnd.choice((0, 1, 2, 7, 7, 2, 3, 6, 8, rnd.getrandbits(32)))
        rig.custom["c621190"] = lambda e: rnd.choice((0, 1, 1, 2, rnd.getrandbits(32)))
        rig.custom["c62ff70"] = lambda e: rnd.choice((0, 1, 1, rnd.getrandbits(32)))
        rig.custom["c63b2c0"] = lambda e: e.r32(e.arg(0) + 0x80)
        rig.custom["c63dfa0"] = lambda e: e.r32(e.arg(0) + 0x10)

        def set_id(e):
            """FUN_00629da0 stand-in: boundary set-item ids around the table's count."""
            count = e.r32(at(TABLE) + 0xC1C)
            return rnd.choice((0, 1, 2, count - 1, count, count + 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF,
                               rnd.getrandbits(32))) & M32
        rig.custom["c629da0"] = set_id

    def at(self, off):
        """Arena address of offset `off`."""
        return self.rig.arena + off

    def unit(self, img, rng, cls=None, kind=0.75, data=0.8, null=0.1):
        """Fill the unit at offset 0 (type, class, data pointer) and return the argument: the unit or null."""
        put(img, 0, 4 if rng.random() < kind else rng.choice((0, 1, 3, 5, 0x10000)))
        put(img, 4, cls if cls is not None else rng.choice((0, 1, 2, 3, 3, 4, 5, 0xFFFFFFFF)))
        put(img, 0x14, self.at(DATA) if rng.random() < data else 0)
        return 0 if rng.random() < null else self.at(0)

    def cls(self, rng):
        """A class id: an Items row of the arena (0..3), a missing row, or a wild id."""
        return rng.choice((0, 1, 2, 3, 3, 4, 5, 0xFFFFFFFF, 0x80000000, rng.getrandbits(32)))

    def stubs(self):
        """FUN_0055d930, 0055e9a0, 0062a360: empty or constant-zero stubs that pop their arguments."""
        rig = self.rig

        def build(img, rng):
            """Random register and stack contents."""
            return {"stack": [rng.getrandbits(32), rng.getrandbits(32)], "regs": {"eax": rng.getrandbits(32)}}
        rig.run("FUN_0055d930", 0x55D930, lambda i, r: {"stack": [r.getrandbits(32)], "regs": {"eax": 0x1234}},
                lambda m, a: 0x1234, 1)
        rig.run("FUN_0055e9a0", 0x55E9A0, build, lambda m, a: 0, 2)
        rig.run("FUN_0062a360", 0x62A360, build, lambda m, a: 0, 2)

    def wrappers(self):
        """The ItemMode.cpp and Items.cpp forwarding wrappers."""
        rig, at = self.rig, self.at

        def b_swap(img, rng):
            """Game and unit in ECX / EDX, owner and apply flag on the stack."""
            return {"ecx": rng.getrandbits(32), "edx": rng.getrandbits(32),
                    "stack": [rng.getrandbits(32), rng.choice((0, 1, rng.getrandbits(32)))]}
        rig.run("FUN_0055c460", 0x55C460, b_swap,
                lambda m, a: m.call("c55c2c0", a["ecx"], a["edx"], a["stack"][0], a["stack"][1]), 2)

        def b_cursor(img, rng):
            """As b_swap with the owner only."""
            return {"ecx": rng.getrandbits(32), "edx": rng.getrandbits(32), "stack": [rng.getrandbits(32)]}
        rig.run("FUN_0055fe60", 0x55FE60, b_cursor,
                lambda m, a: m.call("c55c2c0", a["ecx"], a["edx"], a["stack"][0], 1 if mut("apply-one") else 0), 1)

        def m_state(m, a):
            """c55dbc0(game, unit, 0), then c621000(unit, 1) only when the stack flag is nonzero."""
            r = m.call("c55dbc0", a["ecx"], a["edx"], 0)
            if a["stack"][0] != (1 if mut("flag-eq-1") else 0):
                r = m.call("c621000", a["edx"], 1)
            return r
        rig.run("FUN_0055fde0", 0x55FDE0, lambda i, r: {"ecx": r.getrandbits(32), "edx": r.getrandbits(32),
                "stack": [r.choice((0, 1, 2, r.getrandbits(32)))]}, m_state, 1)

        def b_ident(img, rng):
            """Unit in EDX (may be null; mode at +0x10), game in ECX, owner on the stack."""
            put(img, 0x10, rng.choice((0, 0, 1, 7)))
            return {"ecx": rng.getrandbits(32), "edx": rng.choice((0, at(0))), "stack": [rng.getrandbits(32)]}

        def m_ident(m, a):
            """c6280d0(unit, 0x10, 1); when the unit is null or in mode 0 and c62ff70(unit, owner) is nonzero: c55c2c0, c55dbc0, c621000."""
            u, owner, game = a["edx"], a["stack"][0], a["ecx"]
            r = m.call("c6280d0", u, 0x10, 1)
            if u == 0 or m.r(u + 0x10) == (1 if mut("mode-1") else 0):
                r = m.call("c62ff70", u, owner)
                if r:
                    m.call("c55c2c0", game, u, owner, 0)
                    m.call("c55dbc0", game, owner, 0)
                    r = m.call("c621000", owner, 1)
            return r
        rig.run("FUN_0055fe00", 0x55FE00, b_ident, m_ident, 1)

        def b_gold(img, rng):
            """Item in ESI, owner (inventory at +0x60) in EDI, game on the stack."""
            put(img, 0x1500 + 0x60, rng.getrandbits(32))
            return {"regs": {"esi": rng.getrandbits(32), "edi": at(0x1500)}, "stack": [rng.getrandbits(32)]}

        def m_gold(m, a):
            """c63d2b0(owner inventory, item), then c55c730(game, item, owner, 1, 1) when c621190(item) is exactly 1."""
            esi, edi = a["regs"]["esi"], a["regs"]["edi"]
            m.call("c63d2b0", m.r(edi + 0x60), esi)
            r = m.call("c621190", esi)
            if r == 1:
                r = m.call("c55c730", a["stack"][0], esi, edi, 1, 1)
            return r
        rig.run("FUN_0055d940", 0x55D940, b_gold, m_gold, 1)

        def b_pair(img, rng):
            """Inventory in ECX, default in EDX, body slot on the stack."""
            return {"ecx": rng.getrandbits(32), "edx": rng.getrandbits(32),
                    "stack": [rng.choice((0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, rng.getrandbits(32)))]}

        def m_pair(m, a):
            """Slots 4<->5, 6<->7 and 11<->12 look the paired slot up in the inventory; any other slot gives the default."""
            pairs = {4: 5, 5: 4, 6: 7, 7: 6, 11: 12 if not mut("pair-11") else 13, 12: 11}
            alt = pairs.get(a["stack"][0], 0)
            return m.call("c63bde0", a["ecx"], alt) if alt else a["edx"]
        rig.run("FUN_0055f2a0", 0x55F2A0, b_pair, m_pair, 1)

        def b_notify(img, rng):
            """Unit in ECX (may be null) with a flag word at +0xc4, client in EDX."""
            return {"ecx": rng.choice((0, at(0))), "edx": rng.getrandbits(32), "regs": {"eax": rng.getrandbits(32)}}

        def m_notify(m, a):
            """c55bed0(unit, client) only for a nonnull unit with bit 0 of +0xc4 set."""
            u = a["ecx"]
            if u and m.r(u + 0xC4) & 1:
                m.call("c55bed0", u, a["edx"])
        rig.run("FUN_0055bf30", 0x55BF30, b_notify, m_notify, 0)

        def b_skill(img, rng):
            """Unit in ECX, skill node in EAX."""
            return {"ecx": rng.getrandbits(32), "regs": {"eax": rng.getrandbits(32)}}

        def m_skill(m, a):
            """c647960(unit, skill) status 2 or 7 gives 0; every other status gives 1."""
            r = m.call("c647960", a["ecx"], a["regs"]["eax"])
            return 0 if r in (2, 3 if mut("status-3") else 7) else 1
        rig.run("FUN_0055c4d0", 0x55C4D0, b_skill, m_skill, 0)

        def b_level(img, rng):
            """Two stack arguments."""
            return {"stack": [rng.getrandbits(32), rng.getrandbits(32)]}
        rig.run("FUN_0062ba60", 0x62BA60, b_level,
                lambda m, a: m.call("c62b5b0", a["stack"][0], a["stack"][1]), 2)
        rig.run("FUN_0062e410", 0x62E410, lambda i, r: {"stack": [r.getrandbits(32) for _ in range(4)]},
                lambda m, a: m.call("c62ae20", *a["stack"], 0x60 if not mut("version") else 0x61), 4)

        def b_wclass(img, rng):
            """A unit (may be null) and a weapon class."""
            return {"stack": [self.unit(img, rng), rng.getrandbits(32)]}

        def m_wclass(m, a):
            """Valid item: c628900(class, wclass); otherwise 0."""
            u, w = a["stack"]
            if not is_item(m, u):
                return 0
            return m.call("c628900", w, m.r(u + 4)) if mut("wclass-swap") else m.call("c628900", m.r(u + 4), w)
        rig.run("FUN_00628950", 0x628950, b_wclass, m_wclass, 2)

    def pure(self):
        """FUN_0055ca00, 0062a250, 0062af50: no callees."""
        rig, at = self.rig, self.at

        def b_codes(img, rng):
            """Two item records (code at +0x80) and two codes from a small set, so every pairing is reached."""
            put(img, 0x80, rng.randrange(0, 3))
            put(img, 0x300 + 0x80, rng.randrange(0, 3))
            return {"ecx": at(0), "edx": at(0x300), "stack": [rng.randrange(0, 3), rng.randrange(0, 3)]}

        def m_codes(m, a):
            """1 when the records' codes equal the two arguments in the same or the swapped order."""
            c1, c2 = m.r(a["ecx"] + 0x80), m.r(a["edx"] + 0x80)
            x, y = a["stack"]
            return int((c1 == x and c2 == y) or (not mut("pair-order") and c1 == y and c2 == x))
        rig.run("FUN_0055ca00", 0x55CA00, b_codes, m_codes, 2)

        def b_quality(img, rng):
            """Group in EAX around 1..8, quality in ECX around 0..20."""
            return {"ecx": rng.choice((0, 1, 0x13, 0x14, 0x15, 0x1F, 0x20, rng.getrandbits(32))),
                    "regs": {"eax": rng.choice((0, 1, 2, 7, 8, 9, 0xFFFFFFFF, rng.getrandbits(32)))}}

        def m_quality(m, a):
            """AL = group << 5 | quality for group 1..8 and quality 0..20 (group 8 wraps in the byte), else 0."""
            g, q = a["regs"]["eax"], a["ecx"]
            if (g - (0 if mut("group0") else 1)) & M32 < 8 and q < 0x15:
                return ((g << 5) + (q & 0x1F)) & 0xFF
            return 0
        rig.run("FUN_0062a250", 0x62A250, b_quality, m_quality, 0, 0xFF)

        def b_clamp(img, rng):
            """Width in ECX (0..40 and wilder), value in EDX, bit writer on the stack."""
            return {"ecx": rng.choice((0, 1, 8, 0x1F, 0x20, 0x21, 0x40, rng.randrange(0, 41))),
                    "edx": rng.choice((0, 1, 0xFF, 0x100, 0x7FFFFFFF, 0xFFFFFFFF, rng.getrandbits(32))),
                    "stack": [rng.getrandbits(32)]}

        def m_clamp(m, a):
            """Width other than 32: value saturated to (1 << (width & 31)) - 1; then c410eb0(writer, value, width)."""
            w, v = a["ecx"], a["edx"]
            if w != 0x20:
                limit = ((1 << (w if mut("shift-mask") else w & 31)) - 1) & M32
                v = min(v, limit)
            return m.call("c410eb0", a["stack"][0], v, w)
        rig.run("FUN_0062af50", 0x62AF50, b_clamp, m_clamp, 1)

    def accessors(self):
        """Item-data getters and setters (FUN_006281b0 ... FUN_00629ea0) and the Items-row getters."""
        rig, at = self.rig, self.at

        def b_unit(img, rng):
            """One unit argument."""
            return {"stack": [self.unit(img, rng, cls=0)]}

        def getter(name, addr, off, low, dword=False):
            """A valid-item getter of D + off; `low` is AL on rejection (0 or 0xff); a dword getter returns 0 on rejection."""
            def model(m, a):
                """A byte getter leaves the unit pointer (wrong type) or the null D in EAX above AL."""
                u = a["stack"][0]
                if is_item(m, u) and m.r(u + 0x14):
                    d = m.r(u + 0x14)
                    if dword:
                        return m.r(d + off)
                    return (d & ~0xFF & M32) | m.r(d + off + (1 if mut("getter-off1") else 0), 1)
                if dword:
                    return 0
                return residue(u if u and m.r(u) != 4 else 0, 0 if mut("exchange-zero") else low)
            rig.run(name, addr, b_unit, model, 1)
        getter("FUN_006281b0", 0x6281B0, 0x14, 0, dword=True)
        getter("FUN_006282a0", 0x6282A0, 0x46, 0xFF)
        getter("FUN_006283f0", 0x6283F0, 0x49, 0)

        def m_set_page(m, a):
            """D + 0x46 gets the low byte of the argument for a valid item with data."""
            u, v = a["stack"]
            if is_item(m, u) and m.r(u + 0x14):
                m.w(m.r(u + 0x14) + 0x46, v, 1)
        rig.run("FUN_006282d0", 0x6282D0, lambda i, r: {"stack": [self.unit(i, r), r.getrandbits(32)]}, m_set_page, 2)

        def m_set_pair(m, a):
            """D + 0x1c and + 0x20 get the two arguments."""
            u, x, y = a["stack"]
            if is_item(m, u) and m.r(u + 0x14):
                d = m.r(u + 0x14)
                m.w(d + 0x1C, x)
                m.w(d + 0x20, y if not mut("pair-20") else x)
        rig.run("FUN_00629ea0", 0x629EA0, lambda i, r: {"stack": [self.unit(i, r), r.getrandbits(32), r.getrandbits(32)]},
                m_set_pair, 3)

        def m_seed(m, a):
            """FUN_00650e30(D + 4) for real: the two seed words become 1 and 0x29a."""
            u = a["stack"][0]
            if is_item(m, u) and m.r(u + 0x14):
                d = m.r(u + 0x14)
                m.w(d + 4, 1)
                m.w(d + 8, 0x29A)
        rig.run("FUN_00627dc0", 0x627DC0, b_unit, m_seed, 1)

        def m_missile(m, a):
            """Valid item: the word at row + 0xfa (the row of classes 0..3 always exists); else -1."""
            u = a["stack"][0]
            if not is_item(m, u):
                return M32
            return m.r(m.call("c6335f0", m.r(u + 4)) + 0xFA, 2)

        def b_rec_ok(img, rng):
            """A unit whose class has an Items row (0..3), so FUN_006288a0 never reads the null row."""
            return {"stack": [self.unit(img, rng, cls=rng.randrange(0, 4))]}
        rig.run("FUN_006288a0", 0x6288A0, b_rec_ok, m_missile, 1)

        def bonus(off):
            """Model for the str / dex bonus getters: 100 for a rejected item, a missing row or a stored 1."""
            def model(m, a):
                """The upper bits keep the unit pointer on rejection and the zero-extended word otherwise."""
                u = a["stack"][0]
                if u == 0:
                    return 0x64
                if m.r(u) != 4:
                    return (u & ~0xFFFF & M32) | 0x64
                rec = m.call("c6335f0", m.r(u + 4))
                if rec == 0:
                    return 0x64
                v = m.r(rec + off, 2)
                return 0x64 if v <= 1 and (v == 1 or mut("bonus-le")) else v
            return model

        def b_bonus(img, rng):
            """A unit with a class row and a bonus word among 0, 1, negative and random."""
            for off in (0x106, 0x108):
                img[RECORDS + 0x300 * 0 + off:RECORDS + 0x300 * 0 + off + 2] = rng.choice(
                    (b"\0\0", b"\1\0", b"\xff\xff", rng.randbytes(2), b"\1\0"))
            return {"stack": [self.unit(img, rng, cls=rng.choice((0, 0, 4)))]}
        rig.run("FUN_00629860", 0x629860, b_bonus, bonus(0x106), 1)
        rig.run("FUN_006298a0", 0x6298A0, b_bonus, bonus(0x108), 1)

        def b_type(img, rng):
            """An item type index around the table's count and a type-row pointer that may be null."""
            count = rng.choice((0, 1, 2, 3, 4))
            put(img, TABLE + 0xBFC, count)
            null_rows = rng.random() < 0.15
            put(img, TABLE + 0xBF8, 0 if null_rows else at(TYPES))
            idx = rng.choice((0, 0, 1, 2, 3, count, count - 1, count + 1, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF))
            if null_rows:
                idx = 0
            return {"stack": [idx & M32]}

        def types(off, size, signed):
            """Model for the type-row getters: 0 for an index out of range or a null row."""
            def model(m, a):
                """Signed index checks against the count at +0xbfc; the row is rows + index * 0xe4."""
                idx = s32(a["stack"][0])
                tbl = at(TABLE)
                count = s32(m.r(tbl + 0xBFC))
                if idx < 0 or idx >= count + (1 if mut("type-count") else 0):
                    return 0
                row = (m.r(tbl + 0xBF8) + idx * 0xE4) & M32
                if row == 0:
                    return 0
                v = m.r(row + off, size)
                return (v - (1 << 16) if v >> 15 else v) & M32 if signed else v
            return model
        rig.run("FUN_00628440", 0x628440, b_type, types(0x0C, 2, True), 1)
        rig.run("FUN_006284c0", 0x6284C0, b_type, types(0x13, 1, False), 1)

        def class_flag(off, residue_low):
            """Model for the class-id byte getters 006298e0 (movzx) and 00629990 (mov al): 0 for a missing row."""
            def model(m, a):
                """Missing row gives EAX = 0; 00629990 keeps the row pointer's upper bits."""
                rec = m.call("c6335f0", a["stack"][0])
                if rec == 0:
                    return 0
                return m.r(rec + off, 1) if residue_low is None else (rec & ~0xFF & M32) | m.r(rec + off, 1)
            return model
        rig.run("FUN_006298e0", 0x6298E0, lambda i, r: {"stack": [self.cls(r)]}, class_flag(0x137, None), 1)
        rig.run("FUN_00629990", 0x629990, lambda i, r: {"stack": [self.cls(r)]}, class_flag(0x138, 0), 1)

    def fatal(self):
        """FUN_00629620, 006296b0, 00628980, 00629a00, 00629d00, 0062a2b0: Items row getters that halt on a missing row."""
        rig = self.rig
        for name, addr, line, off, size, mask in (
                ("FUN_00629620", 0x629620, 0xD6C, 0xEC, 4, M32), ("FUN_006296b0", 0x6296B0, 0xD8A, 0xE4, 4, M32),
                ("FUN_00628980", 0x628980, 0x906, 0x11C, 1, M32), ("FUN_00629a00", 0x629A00, 0xF28, 0x13E, 1, M32),
                ("FUN_00629d00", 0x629D00, 0x1054, 0xDC, 4, 4)):
            def model(m, a, line=line, off=off, size=size, mask=mask):
                """Missing row: fatal at `line`; otherwise the field at row + off (ANDed with `mask`)."""
                rec = m.call("c6335f0", a["stack"][0])
                if rec == 0:
                    raise Halt(line + (1 if mut("fatal-line") else 0))
                return m.r(rec + off, size) & mask
            rig.run(name, addr, lambda i, r: {"stack": [self.cls(r)]}, model, 1)

        def b_compact(img, rng):
            """Item in EAX (may be null) of any type; the class decides the row."""
            return {"regs": {"eax": self.unit(img, rng, cls=self.cls(rng), kind=0.5)}}

        def m_compact(m, a):
            """Class -1 for a null unit, no type guard; fatal 0x12e0 without a row, else byte row + 0x143."""
            u = a["regs"]["eax"]
            rec = m.call("c6335f0", m.r(u + 4) if u else M32)
            if rec == 0:
                raise Halt(0x12E0)
            return m.r(rec + 0x143, 1)
        rig.run("FUN_0062a2b0", 0x62A2B0, b_compact, m_compact, 0)

    def lifecycle(self):
        """FUN_00627d00 (free), FUN_0062a900 (container count), FUN_0062a490 (set item row)."""
        rig, at = self.rig, self.at

        def b_free(img, rng):
            """Pool and unit on the stack; the unit's data pointer may be null."""
            return {"stack": [rng.getrandbits(32), self.unit(img, rng)]}

        def m_free(m, a):
            """Valid item: c63aaf0(unit), then free D (pool, D, Items.cpp, line 0x10a, 0) when D is set."""
            pool, u = a["stack"]
            if not is_item(m, u):
                return None
            m.call("c63aaf0", u)
            d = m.r(u + 0x14)
            if d or mut("free-null"):
                m.log.append(("free", pool, d, ITEMS_CPP, 0x10A, 0))
            return None
        rig.run("FUN_00627d00", 0x627D00, b_free, m_free, 2)

        def b_count(img, rng):
            """An item (EAX) whose row flag (+0x137) and inventory (+0x60, head at +0x80, chain at the nodes) vary."""
            u = self.unit(img, rng, cls=rng.choice((0, 1, 4)), null=0.05)
            for k in range(2):
                img[RECORDS + 0x300 * k + 0x137] = rng.choice((0, 1, 1, 2))
            put(img, 0x60, rng.choice((0, at(INV), at(INV))))
            n = rng.choice((0, 1, 2, 3, 5))
            nodes = [at(NODES + 0x40 * i) for i in range(n)]
            put(img, INV + 0x80, nodes[0] if nodes else 0)
            for i in range(n):
                put(img, NODES + 0x40 * i + 0x10, nodes[i + 1] if i + 1 < n else 0)
            return {"regs": {"eax": u}}

        def m_count(m, a):
            """Valid item with a row whose +0x137 is nonzero and an inventory: count the chain of c63b2c0 / c63dfa0."""
            u = a["regs"]["eax"]
            if not is_item(m, u):
                return 0
            rec = m.call("c6335f0", m.r(u + 4))
            if rec == 0 or m.r(rec + 0x137, 1) == 0:
                return 0
            inv = m.r(u + 0x60)
            if inv == 0:
                return 0
            node = m.call("c63b2c0", inv)
            n = 0
            while node:
                n += 1
                node = m.call("c63dfa0", node)
            return n - (1 if mut("count-first") and n else 0)
        rig.run("FUN_0062a900", 0x62A900, b_count, m_count, 0)

        def b_set(img, rng):
            """An item whose data holds quality 5 (or not) and a set-item table of 0..4 rows."""
            u = self.unit(img, rng, cls=rng.choice((0, 1)), data=0.9, null=0.05)
            put(img, DATA, rng.choice((5, 5, 5, 4, 0, rng.getrandbits(32))))
            put(img, TABLE + 0xC1C, rng.choice((0, 1, 3, 4, 0x7FFFFFFF)))
            put(img, TABLE + 0xC18, rng.choice((0, at(0x3900), rng.getrandbits(32))))
            return {"stack": [u]}

        def m_set(m, a):
            """Quality-5 item with data: id = c629da0(unit); 0 <= id < count gives base + id * 0x1b8, else 0."""
            u = a["stack"][0]
            if not is_item(m, u) or m.r(u + 0x14) == 0 or m.r(m.r(u + 0x14)) != (4 if mut("quality-4") else 5):
                return 0
            idx = s32(m.call("c629da0", u))
            tbl = at(TABLE)
            if idx < 0 or idx >= s32(m.r(tbl + 0xC1C)):
                return 0
            return (m.r(tbl + 0xC18) + idx * 0x1B8) & M32
        rig.run("FUN_0062a490", 0x62A490, b_set, m_set, 1)


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 80
    s = Suite(rig)
    s.stubs()
    s.wrappers()
    s.pure()
    s.accessors()
    s.fatal()
    s.lifecycle()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("type3", "getter-off1", "apply-one", "mode-1", "pair-11", "status-3", "wclass-swap", "pair-order",
                 "group0", "flag-eq-1", "version", "pair-20", "type-count", "shift-mask", "exchange-zero", "bonus-le", "fatal-line", "free-null", "quality-4", "count-first")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "ITEMS small function", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
