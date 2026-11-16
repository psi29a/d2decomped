# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 37 more small ITEMS functions (Items.cpp, ItemMode.cpp) in stock game.exe 1.14d.

    uv run python items_traces2.py [--mutation-check | --coverage]

The sequel to items_traces.py, on the same rig (_dungeon_rig.py). Runs game.exe's
own code for the Items.cpp getters and setters FUN_00627d90, 006282f0,
00629f20, 0062a670, 00627e20, 00629e40, 00627c90 (item data), 0062a060,
00629660, 006296f0, 00629a40, 00628530, 00629800, 0062a180 (Items rows),
0062e7e0, 0062e830, 0062bbd0, 0062c0b0, 0062ea80 (ItemTypes rows), 0062b3a0
(SetItems row), 0062a310 (quantity), 0062b450 (socketed stat sum), and the
ItemMode.cpp helpers FUN_0055c270, 0055c6e0 (book charges in / out),
0055c470 (wielded weapons), 0055c4f0 (a kept hand skill back), 0055c560
(skill on a weapon type), 0055d9d0 (vitals snapshot), 0055f500 (refresh
socketed stats), 0055f5d0 (same book), 0055f6e0 (set quantity), 0055f9c0
(zero the item skill), 0055fb10 (to the cursor), 0055ed30 (back to its
cell), 00561e70 (told, then back), 00562d30 (timer 3), 00562da0 (item timer
dispatch) against hand-written models on random unit, item-data and table
bytes: the whole EAX, the ordered callee log with register and stack
arguments, the arena and allocated blocks, the fatal-error line and the
bytes `ret n` pops. Run for real inside them: FUN_00629620, 006296b0,
00629a00, 0055c4d0 (verified by items_traces.py), the CRT memset, the bare
`ret` at 0x55f120 (timer 4) and, where they call each other, 0055d9d0,
0055ed30 and 00562d30. Every other callee is hooked and logged, not claimed.
`--mutation-check` breaks the model 24 ways; `--coverage` lists any instruction
of the 37 bodies the cases never reach (none, apart from the fatal-exit tails).
"""
# gaps-runs: 00627d90 006282f0 00629f20 0062a670 00627e20 00629e40 00627c90 0062a060 00629660 006296f0
# gaps-runs: 00629a40 00628530 00629800 0062a180 0062e7e0 0062e830 0062bbd0 0062c0b0 0062ea80 0062b3a0
# gaps-runs: 0062a310 0062b450 0055c270 0055c6e0 0055c470 0055c4f0 0055c560 0055d9d0 0055f500 0055f5d0
# gaps-runs: 0055f6e0 0055f9c0 0055fb10 0055ed30 00561e70 00562d30 00562da0
import argparse
import sys
from pathlib import Path

import capstone
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_ESI

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt, mix, s32

MUT = set()
DATA = 0x400            # item data (unit + 0x14)
RECORDS = 0x800         # Items rows, 0x300 apart, classes 0..3
PLAYER = 0x1480         # a second unit: the player (type +0, id +0xc, inventory +0x60)
OUT = 0x1600            # out-parameters the functions write
ROW = 0x1700            # a skills.txt row (FUN_00644140's answer)
PATH = 0x1780           # an item's path / room record (unit + 0x2c)
INV = 0x1800            # inventory pointers
NODES = 0x2000          # inventory nodes, 0x40 apart: next at +0x10, item at +0x14
TABLE = 0x2800          # the stand-in sgptDataTable (global 0x744304)
TYPES = 0x3500          # ItemTypes rows, 0xe4 apart
SETS = 0x3900           # SetItems rows, 0x1b8 apart
ITEMS_CPP = 0x6E_1150   # ".\\ITEMS\\Items.cpp"
ITEMMODE_CPP = 0x6E_1164  # ".\\ITEMS\\ItemMode.cpp"
TIMERS = 0x6E_117C      # item timer table: entry 3 FUN_00562d30, entry 4 a bare ret


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def put(img, off, value, size=4):
    """Write a little-endian value at arena offset `off`."""
    img[off:off + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little")


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig):
        """Hook the unclaimed callees and point the data-table global at the arena."""
        self.rig = rig
        self.pick = {}          # per-case answers the custom hooks give (set by the builders)
        rig.m.w32(0x744304, rig.arena + TABLE)
        rnd = rig.rng
        for name, addr, nstack, regs in (
                ("c6335f0", 0x63_35F0, 1, ()), ("c62b400", 0x62_B400, 1, ()), ("c629da0", 0x62_9DA0, 1, ()),
                ("c625480", 0x62_5480, 3, ()), ("c625500", 0x62_5500, 3, ()), ("c627260", 0x62_7260, 4, ()),
                ("c6295b0", 0x62_95B0, 1, ()), ("c63b2c0", 0x63_B2C0, 1, ()), ("c63dfa0", 0x63_DFA0, 1, ()),
                ("c63dfd0", 0x63_DFD0, 1, ()), ("c451f70", 0x45_1F70, 0, ("ecx",)),
                ("c55c110", 0x55_C110, 1, ("eax", "edi")), ("c63bef0", 0x63_BEF0, 1, ()),
                ("c63bde0", 0x63_BDE0, 2, ()), ("c629bb0", 0x62_9BB0, 2, ()),
                ("c6439b0", 0x64_39B0, 1, ("ecx", "edx")), ("c647960", 0x64_7960, 2, ()),
                ("c5701b0", 0x57_01B0, 2, ("ecx", "edx")), ("c644140", 0x64_4140, 0, ("ecx",)),
                ("c629b50", 0x62_9B50, 2, ()), ("c620190", 0x62_0190, 1, ()), ("c6201d0", 0x62_01D0, 1, ()),
                ("c643ad0", 0x64_3AD0, 1, ()), ("c643ce0", 0x64_3CE0, 3, ()),
                ("c6277f0", 0x62_77F0, 2, ()), ("c627910", 0x62_7910, 3, ()), ("c627ec0", 0x62_7EC0, 2, ()),
                ("c627f80", 0x62_7F80, 2, ()), ("c627f20", 0x62_7F20, 1, ()), ("c6272b0", 0x62_72B0, 4, ()),
                ("c5531c0", 0x55_31C0, 1, ("ecx", "edx")), ("c53d130", 0x53_D130, 4, ("ecx", "edx")),
                ("c55c070", 0x55_C070, 1, ("eax",)), ("c55bff0", 0x55_BFF0, 0, ("esi",)),
                ("c6450f0", 0x64_50F0, 1, ()), ("c645120", 0x64_5120, 2, ()),
                ("c624690", 0x62_4690, 2, ()), ("c628170", 0x62_8170, 3, ()), ("c63cc70", 0x63_CC70, 2, ()),
                ("c621000", 0x62_1000, 2, ()), ("c53eed0", 0x53_EED0, 2, ("ecx", "edx")),
                ("c63ad90", 0x63_AD90, 2, ()), ("c6488c0", 0x64_88C0, 1, ()), ("c63c550", 0x63_C550, 2, ()),
                ("c557fd0", 0x55_7FD0, 0, ("ecx", "edx")), ("c6280a0", 0x62_80A0, 4, ()),
                ("c629930", 0x62_9930, 1, ()), ("c625e00", 0x62_5E00, 1, ()),
                ("c562c40", 0x56_2C40, 2, ("eax", "ebx")), ("c6289f0", 0x62_89F0, 1, ())):
            rig.callee(name, addr, nstack, regs)
        self.out_hooks()
        at = self.at

        def row(e):
            """FUN_006335f0 stand-in: the arena's Items row for classes 0..3, else none."""
            return at(RECORDS) + 0x300 * e.arg(0) if e.arg(0) < 4 else 0

        def chain(off):
            """Node walk stand-ins: the dword at node + off, and 0 for a null node (no read)."""
            return lambda e: e.r32(e.arg(0) + off) if e.arg(0) else 0

        def small(e):
            """A small answer, so the equality tests on hooked results both pass and fail."""
            return rnd.choice((0, 1, 2, 0x10001, rnd.getrandbits(32)))

        def status(e):
            """FUN_00647960: a skill status around the refused 2 and 7."""
            return rnd.choice((0, 1, 2, 2, 3, 6, 7, 7, 8, rnd.getrandbits(32)))

        custom = rig.custom
        custom["c6335f0"] = row
        custom["c63b2c0"] = chain(0x80)
        custom["c63dfa0"] = chain(0x10)
        custom["c63dfd0"] = chain(0x14)
        custom["c62b400"] = lambda e: self.pick.get("type", 0)
        custom["c629da0"] = lambda e: self.pick.get("set", 0)
        custom["c644140"] = lambda e: self.pick.get("row", 0)
        custom["c63ad90"] = lambda e: self.pick.get("cell", 0)
        custom["c6439b0"] = lambda e: rnd.choice((0, 0, e.mu.reg_read(UC_X86_REG_EDI), rnd.getrandbits(32) | 1))
        custom["c647960"] = status
        custom["c451f70"] = lambda e: rnd.choice((0, 1, 1, 2, rnd.getrandbits(32)))
        for name in ("c625480", "c625500", "c6295b0", "c629bb0", "c629b50", "c620190", "c6201d0",
                     "c6280a0", "c629930", "c6289f0", "c562c40"):
            custom[name] = small
        for name in ("c63bef0", "c63bde0", "c55bff0"):
            custom[name] = lambda e: rnd.choice((0, 1, 2, 0xFFFFFFFF, 0x10001))
        for name in ("c627ec0", "c627f80", "c627f20"):
            # mostly equal, and 0x10005 equal to 5 only in its low word: FUN_0055f5d0 compares 16 bits
            custom[name] = lambda e: rnd.choice((5, 5, 5, 5, 5, 0x10005, 6))

    def out_hooks(self):
        """Callees that write through a pointer to the caller's locals: logged without the pointer."""
        rig = self.rig
        rnd = rig.rng

        def outs2(name):
            """FUN_00623060 / FUN_006230c0 (unit, &skill, &owner): log the unit, write two values."""
            def hook(e):
                """The values are mix() of the unit, so the model knows them."""
                unit = e.arg(0)
                rig.log.append((name, unit))
                e.w32(e.arg(1), mix(name + "a", unit))
                e.w32(e.arg(2), mix(name + "b", unit))
                rig.outs.append(0)
                return 0
            return hook
        rig.m.hook(0x62_3060, outs2("c623060"), 3)
        rig.m.hook(0x62_30C0, outs2("c6230c0"), 3)

        def flag(e):
            """FUN_006287b0 (&out, 0x20, 1): log the two values, write a random dword the model reads back."""
            value = rnd.choice((0, 1, rnd.getrandbits(32)))
            rig.log.append(("c6287b0", e.arg(1), e.arg(2)))
            e.w32(e.arg(0), value)
            rig.outs.append(value)
            return value
        rig.m.hook(0x62_87B0, flag, 3)

        def stat_value(e):
            """FUN_00625820 (item, &value): log the item, write mix() of it, give a small answer."""
            item = e.arg(0)
            answer = rnd.choice((0, 1, 1, rnd.getrandbits(32)))
            rig.log.append(("c625820", item))
            e.w32(e.arg(1), mix("c625820v", item))
            rig.outs.append(answer)
            return answer
        rig.m.hook(0x62_5820, stat_value, 2)

        def refresh(e):
            """FUN_0055da70 (ESI unit, EDI the 0x1c-byte snapshot on the stack): log the unit and the snapshot."""
            rig.log.append(("c55da70", e.mu.reg_read(UC_X86_REG_ESI), e.read(e.mu.reg_read(UC_X86_REG_EDI), 0x1C)))
            rig.outs.append(0)
            return 0
        rig.m.hook(0x55_DA70, refresh, 0)

        def told(e):
            """FUN_0053c520 (ECX client, DL kind; value, skill, 0): of EDX only DL is set by the caller."""
            rig.log.append(("c53c520", e.mu.reg_read(UC_X86_REG_ECX), e.mu.reg_read(UC_X86_REG_EDX) & 0xFF,
                            e.arg(0), e.arg(1), e.arg(2)))
            rig.outs.append(0)
            return 0
        rig.m.hook(0x53_C520, told, 3)

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

    def data_or_halt(self, m, u, lines):
        """The fatal guard chain of the Items.cpp getters: null unit, wrong type, null D (lines)."""
        if u == 0:
            raise Halt(lines[0])
        if m.r(u) != 4:
            raise Halt(lines[1])
        d = m.r(u + 0x14)
        if d == 0:
            raise Halt(lines[2])
        return d

    def items_data(self):
        """Item-data getters and setters."""
        rig, at = self.rig, self.at

        def b_unit(img, rng):
            """One item argument."""
            return {"stack": [self.unit(img, rng)]}

        def m_seed_ptr(m, a):
            """FUN_00627d90: D + 4 (the seed) for a valid item with data, else 0."""
            d = self.data(m, a["stack"][0])
            return (d + (4 if not mut("seed-off") else 8)) & M32 if d else 0
        rig.run("FUN_00627d90", 0x627D90, b_unit, m_seed_ptr, 1)

        def m_byte47(m, a):
            """FUN_006282f0: byte D + 0x47 in AL over D's upper bits; else AL = 0xff over the unit (wrong type) or 0."""
            u = a["stack"][0]
            d = self.data(m, u)
            if d:
                return (d & ~0xFF & M32) | m.r(d + 0x47, 1)
            high = u if u and m.r(u) != 4 else 0
            return (high & ~0xFF & M32) | 0xFF
        rig.run("FUN_006282f0", 0x6282F0, b_unit, m_byte47, 1)

        def m_dword_c(m, a):
            """FUN_00629f20: fatal 0x10c7 / 0x10c8 / 0x10ca, else dword D + 0xc."""
            return m.r(self.data_or_halt(m, a["stack"][0], (0x10C7, 0x10C8, 0x10CA)) + 0xC)
        rig.run("FUN_00629f20", 0x629F20, b_unit, m_dword_c, 1)

        def m_word30(m, a):
            """FUN_0062a670: fatal 0x1504 / 0x1505 / 0x1507, else AX = word D + 0x30 over D's upper half."""
            d = self.data_or_halt(m, a["stack"][0], (0x1504, 0x1505, 0x1507 if not mut("line-1506") else 0x1506))
            return (d & 0xFFFF0000) | m.r(d + 0x30, 2)
        rig.run("FUN_0062a670", 0x62A670, b_unit, m_word30, 1)

        def m_set10(m, a):
            """FUN_00627e20: fatal 0x187 / 0x188 / 0x18b, else D + 0x10 = the value."""
            u, v = a["stack"]
            m.w(self.data_or_halt(m, u, (0x187, 0x188, 0x18B)) + 0x10, v)
        rig.run("FUN_00627e20", 0x627E20, lambda i, r: {"stack": [self.unit(i, r), r.getrandbits(32)]}, m_set10, 2)

        def b_pair(img, rng):
            """An item and two distinct out-pointers."""
            return {"stack": [self.unit(img, rng), at(OUT), at(OUT + 8)]}

        def m_get_pair(m, a):
            """FUN_00629e40: *x, *y = D + 0x1c, D + 0x20 for a valid item with data, else both 0."""
            u, x, y = a["stack"]
            d = self.data(m, u)
            m.w(x, m.r(d + 0x1C) if d else 0)
            m.w(y, m.r(d + (0x20 if not mut("pair-1c") else 0x1C)) if d else 0)
        rig.run("FUN_00629e40", 0x629E40, b_pair, m_get_pair, 3)

        def b_alloc(img, rng):
            """Pool and item; the allocator sometimes fails."""
            self.pick["fail"] = rng.random() < 0.15
            return {"stack": [rng.getrandbits(32), self.unit(img, rng, data=0.5)]}

        def m_alloc(m, a):
            """FUN_00627c90: for a valid item, 0x74 zeroed bytes (Items.cpp line 0xd2) with +0xc = -1 into unit + 0x14; a null block is fatal 0xd5."""
            pool, u = a["stack"]
            if not self.is_item(m, u):
                return None
            if self.pick["fail"]:
                m.log.append(("alloc", pool, 0x74, ITEMS_CPP, 0xD2, 0))
                raise Halt(0xD5)
            block = m.alloc(pool, 0x74, 0xD2, ITEMS_CPP)
            m.w(block + 0xC, M32 if not mut("alloc-c0") else 0)
            m.w(u + 0x14, block)
            return None
        rig.custom["alloc"] = lambda e: 0 if self.pick["fail"] else e.alloc(max(e.mu.reg_read(UC_X86_REG_EDX), 1))
        rig.run("FUN_00627c90", 0x627C90, b_alloc, m_alloc, 2)
        del rig.custom["alloc"]

    def items_rows(self):
        """Items-row getters: by class (fatal on a missing row) and by item."""
        rig = self.rig

        def b_cls(img, rng):
            """A class id: rows 0..3, a missing row, a wild id."""
            return {"stack": [rng.choice((0, 1, 2, 3, 4, 0xFFFFFFFF, rng.getrandbits(32)))]}

        def m_tmog(m, a):
            """FUN_0062a060: fatal 0x1155 without a row, else Items byte + 0x139 (Transmogrify) != 0."""
            rec = m.call("c6335f0", a["stack"][0])
            if rec == 0:
                raise Halt(0x1155)
            return int(m.r(rec + (0x139 if not mut("tmog-off") else 0x13A), 1) != 0)
        def b_tmog(img, rng):
            """b_cls with each row's Transmogrify byte zero half the time (its neighbour the other way)."""
            for k in range(4):
                zero = rng.random() < 0.5
                img[RECORDS + 0x300 * k + 0x139] = 0 if zero else rng.randrange(1, 256)
                img[RECORDS + 0x300 * k + 0x13A] = rng.randrange(1, 256) if zero else 0
            return b_cls(img, rng)
        rig.run("FUN_0062a060", 0x62A060, b_tmog, m_tmog, 1)

        def b_item(img, rng):
            """An item whose class may lack a row."""
            return {"stack": [self.unit(img, rng)]}

        def by_item(lines, field):
            """Model of the item wrappers: fatal on null / wrong type, then the class getter (fatal on no row)."""
            def model(m, a):
                """The class getter runs for real; its row lookup is hooked."""
                u = a["stack"][0]
                if u == 0:
                    raise Halt(lines[0])
                if m.r(u) != 4:
                    raise Halt(lines[1])
                rec = m.call("c6335f0", m.r(u + 4))
                if rec == 0:
                    raise Halt(lines[2])
                return field(m, rec)
            return model
        rig.run("FUN_00629660", 0x629660, b_item, by_item((0xD7B, 0xD7C, 0xD6C), lambda m, r: m.r(r + 0xEC)), 1)
        rig.run("FUN_006296f0", 0x6296F0, b_item, by_item((0xD93, 0xD94, 0xD8A), lambda m, r: m.r(r + 0xE4)), 1)
        rig.run("FUN_00629a40", 0x629A40, b_item,
                by_item((0xF3D, 0xF3E if not mut("line-f3f") else 0xF3F, 0xF28), lambda m, r: m.r(r + 0x13E, 1)), 1)
        rig.run("FUN_00628530", 0x628530, b_item, by_item((0x608, 0x609, 0x60C), lambda m, r: m.r(r + 0xC4)), 1)

        def low_byte(off):
            """`mov al` getters: the row's upper 24 bits stay in EAX."""
            return lambda m, r: (r & ~0xFF & M32) | m.r(r + off, 1)
        rig.run("FUN_00629800", 0x629800, b_item, by_item((0xDCF, 0xDD0, 0xDD2), low_byte(0x136)), 1)
        rig.run("FUN_0062a180", 0x62A180, b_item,
                by_item((0x118F, 0x1190, 0x1192), low_byte(0x13C if not mut("sound-off") else 0x13B)), 1)

    def type_rows(self):
        """ItemTypes and SetItems row getters through the item's type / set id."""
        rig, at = self.rig, self.at

        def b_type(img, rng):
            """An item, a type count of 0..4 and a type index around it; null rows only with index 0."""
            count = rng.choice((0, 1, 2, 3, 4))
            put(img, TABLE + 0xBFC, count)
            null_rows = rng.random() < 0.15
            put(img, TABLE + 0xBF8, 0 if null_rows else at(TYPES))
            idx = 0 if null_rows else rng.choice((0, 1, 2, 3, count, count - 1, count + 1, -1, 0x80000000, 0x7FFFFFFF))
            self.pick["type"] = idx & M32
            for k in range(4):
                if rng.random() < 0.5:
                    img[TYPES + 0xE4 * k + 0x21] = rng.choice((0, 6, 7, 8, 0xFF))
            return {"stack": [self.unit(img, rng)], "outs": [at(OUT), at(OUT + 8)]}

        def type_row(m, u):
            """The item's ItemTypes row, or 0: valid item, signed index in [0, count), rows + index * 0xe4 nonzero."""
            if not self.is_item(m, u):
                return 0
            idx = s32(m.call("c62b400", u))
            tbl = at(TABLE)
            if idx < 0 or idx >= s32(m.r(tbl + 0xBFC)) + (1 if mut("type-count") else 0):
                return 0
            return (m.r(tbl + 0xBF8) + idx * 0xE4) & M32

        def type_byte(off, default):
            """Zero-extended byte of the row, `default` without one."""
            def model(m, a):
                """ItemTypes byte getter."""
                r = type_row(m, a["stack"][0])
                return m.r(r + off, 1) if r else default
            return model
        rig.run("FUN_0062e7e0", 0x62E7E0, b_type, type_byte(0x11, 0), 1)
        rig.run("FUN_0062e830", 0x62E830, b_type, type_byte(0x12, 0), 1)
        rig.run("FUN_0062bbd0", 0x62BBD0, b_type, type_byte(0x1F, 7 if not mut("default-0") else 0), 1)

        def m_capped(m, a):
            """FUN_0062c0b0: the row's byte + 0x21 when below 7 (unsigned), else 7; 7 without a row."""
            r = type_row(m, a["stack"][0])
            v = m.r(r + 0x21, 1) if r else 7
            return v if v < (7 if not mut("cap-6") else 6) else 7
        rig.run("FUN_0062c0b0", 0x62C0B0, b_type, m_capped, 1)

        def b_type_pair(img, rng):
            """b_type with two out-pointers on the stack."""
            case = b_type(img, rng)
            return {"stack": case["stack"] + case["outs"]}

        def m_type_pair(m, a):
            """FUN_0062ea80: *x, *y = row bytes + 0xa, + 0xb, or both 0 without a row (byte writes)."""
            u, x, y = a["stack"]
            r = type_row(m, u)
            m.w(x, m.r(r + 0xA, 1) if r else 0, 1)
            m.w(y, m.r(r + 0xB, 1) if r else 0, 1)
        rig.run("FUN_0062ea80", 0x62EA80, b_type_pair, m_type_pair, 3)

        def b_set(img, rng):
            """A quality-5 item (or not), a set table of 0..4 rows or huge counts, ids that stay inside the arena."""
            u = self.unit(img, rng, cls=0, data=0.9, null=0.05)
            put(img, DATA, rng.choice((5, 5, 5, 4, 0, rng.getrandbits(32))))
            count = rng.choice((0, 1, 3, 4, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF))
            put(img, TABLE + 0xC1C, count)
            null_rows = rng.random() < 0.15
            put(img, TABLE + 0xC18, 0 if null_rows else at(SETS))
            self.pick["set"] = 0 if null_rows else rng.choice((0, 1, 2, 3, count, count + 1, 0xFFFFFFFF)) & M32
            if not null_rows and self.pick["set"] > 3 and s32(self.pick["set"]) < s32(count):
                self.pick["set"] = 3
            return {"stack": [u]}

        def m_set_word(m, a):
            """FUN_0062b3a0: valid quality-5 item with data: id in [0, count) gives SetItems word + 0x2e (signed), else -1."""
            u = a["stack"][0]
            d = self.data(m, u)
            if not d or m.r(d) != 5:
                return M32
            idx = s32(m.call("c629da0", u))
            tbl = at(TABLE)
            if idx < 0 or idx >= s32(m.r(tbl + 0xC1C)):
                return M32
            r = (m.r(tbl + 0xC18) + idx * 0x1B8) & M32
            if r == 0:
                return M32
            v = m.r(r + 0x2E, 2)
            return (v - 0x10000 if v >> 15 and not mut("set-unsigned") else v) & M32
        rig.run("FUN_0062b3a0", 0x62B3A0, b_set, m_set_word, 1)

    def items_stats(self):
        """FUN_0062a310 (quantity left) and FUN_0062b450 (a stat summed over socketed children)."""
        rig, at = self.rig, self.at

        def m_quantity(m, a):
            """Quantity (stat 0x46) above 0: 1; else with stat 0x7d set: quantity = 0 and 1; else 1 when the max is 0 or less."""
            u = a["stack"][0]
            if u == 0:
                return 0
            if s32(m.call("c625480", u, 0x46, 0)) > 0:
                return 1
            if m.call("c625500", u, 0x7D, 0):
                m.call("c627260", u, 0x46, 0, 0)
                return 1
            return int(s32(m.call("c6295b0", u)) <= (0 if not mut("max-lt") else -1))
        rig.run("FUN_0062a310", 0x62A310, lambda i, r: {"stack": [r.choice((0, at(0), r.getrandbits(32)))]},
                m_quantity, 1)

        def b_sum(img, rng):
            """Parent item in EBX with a row flag + 0x137, an inventory chain of 0..4 children; stat and &sum on the stack."""
            u = self.unit(img, rng, cls=rng.choice((0, 1, 4)), null=0.05)
            for k in range(2):
                img[RECORDS + 0x300 * k + 0x137] = rng.choice((0, 1, 1, 7))
            put(img, 0x60, rng.choice((0, at(INV), at(INV))))
            n = rng.choice((0, 1, 2, 4))
            nodes = [at(NODES + 0x40 * i) for i in range(n)]
            put(img, INV + 0x80, nodes[0] if nodes else 0)
            for i in range(n):
                put(img, NODES + 0x40 * i + 0x10, nodes[i + 1] if i + 1 < n else 0)
            return {"regs": {"ebx": u}, "stack": [rng.getrandbits(32), at(OUT)]}

        def m_sum(m, a):
            """*sum = 0; for a valid item whose row's + 0x137 is set, each child's stat is added while the PARENT's mode (FUN_00451f70) is 1."""
            u, (stat, out) = a["regs"]["ebx"], a["stack"]
            m.w(out, 0)
            if not self.is_item(m, u):
                return None
            rec = m.call("c6335f0", m.r(u + 4))
            if rec == 0 or m.r(rec + 0x137, 1) == 0:
                return None
            node = m.call("c63b2c0", m.r(u + 0x60))
            while node:
                child = m.call("c63dfd0", node)
                if m.call("c451f70", child if mut("child-mode") else u) == 1:
                    m.w(out, m.r(out) + m.call("c625480", child, stat, 0))
                node = m.call("c63dfa0", node)
            return None
        rig.run("FUN_0062b450", 0x62B450, b_sum, m_sum, 2)

    def mode_small(self):
        """ItemMode.cpp: book charges, wielded weapons, hand skills."""
        rig, at = self.rig, self.at

        def b_book(img, rng):
            """Player in ECX (type 0 or not, may be null), item in EDX (may be null)."""
            put(img, PLAYER, rng.choice((0, 0, 1, 3)))
            return {"ecx": rng.choice((0, at(PLAYER), at(PLAYER), at(PLAYER))), "edx": rng.choice((0, rng.getrandbits(32) | 1))}

        def book(lines, add):
            """FUN_0055c270 / 0055c6e0: fatal on a null player / item, then FUN_0055c110(add) for a player (type 0)."""
            def model(m, a):
                """FUN_0055c110 takes the item in EAX, the player in EDI and the flag on the stack."""
                p, item = a["ecx"], a["edx"]
                if p == 0:
                    raise Halt(lines[0])
                if item == 0:
                    raise Halt(lines[1])
                if m.r(p) == 0:
                    m.call("c55c110", item, p, add)
            return model
        rig.run("FUN_0055c270", 0x55C270, b_book, book((0x24D, 0x24E), 1), 0)
        rig.run("FUN_0055c6e0", 0x55C6E0, b_book, book((0x394, 0x395), 0 if not mut("book-out-1") else 1), 0)

        def b_wield(img, rng):
            """Unit in EAX with an inventory (or none); &wielded in EDI, &other in EBX."""
            put(img, 0x60, rng.choice((0, at(INV))))
            return {"regs": {"eax": at(0), "edi": at(OUT), "ebx": at(OUT + 8)}}

        def m_wield(m, a):
            """*w = the wielded item; *o = the other arm (slot 5 when slot 4 holds it, else slot 4), cleared unless it is a weapon (0x2d)."""
            w, o = a["regs"]["edi"], a["regs"]["ebx"]
            m.w(o, 0)
            m.w(w, 0)
            inv = m.r(a["regs"]["eax"] + 0x60)
            if inv == 0:
                return None
            m.w(w, m.call("c63bef0", inv))
            right = m.call("c63bde0", inv, 4)
            left = m.call("c63bde0", inv, 5)
            m.w(o, left if m.r(w) == right and not mut("arm-swap") else right)
            if m.r(o) and m.call("c629bb0", m.r(o), 0x2D) == 0:
                m.w(o, 0)
            return None
        rig.run("FUN_0055c470", 0x55C470, b_wield, m_wield, 0)

        def b_keep(img, rng):
            """Unit in ESI, the skill to skip in EDI, the hand flag in EBX (0: FUN_00623060, else FUN_006230c0)."""
            return {"regs": {"esi": rng.choice((0, rng.getrandbits(32) | 1, rng.getrandbits(32) | 1)),
                             "edi": rng.choice((0, rng.getrandbits(32) | 1, rng.getrandbits(32) | 1)),
                             "ebx": rng.choice((0, 1, rng.getrandbits(32)))}}

        def m_keep(m, a):
            """FUN_0055c4f0: the kept skill and owner of the hand; the unit's skill node unless null, EDI, or status 2 / 7; then FUN_005701b0."""
            u, skip, hand = a["regs"]["esi"], a["regs"]["edi"], a["regs"]["ebx"]
            if u == 0 or skip == 0:
                return None
            name = "c6230c0" if hand else "c623060"
            m.call(name, u)
            skill, owner = mix(name + "a", u), mix(name + "b", u)
            node = m.call("c6439b0", u, skill, owner)
            if node == 0 or node == skip:
                return None
            if m.call("c647960", u, node) in (2, 7 if not mut("status-6") else 6):
                return None
            m.call("c5701b0", u, hand, skill, owner)
            return None
        rig.run("FUN_0055c4f0", 0x55C4F0, b_keep, m_keep, 0)

        def b_weapon_skill(img, rng):
            """Skill node in EDI (may be null), unit on the stack; the skills row's type word + 0x18 and byte + 0x14 vary."""
            put(img, ROW + 0x18, rng.choice((0, 1, 0x30, 0x7FFF, 0x8000, 0xFFFF)), 2)
            img[ROW + 0x14] = rng.choice((2, 2, 1, 3))
            self.pick["row"] = rng.choice((0, at(ROW), at(ROW), at(ROW)))
            return {"regs": {"edi": rng.choice((0, rng.getrandbits(32) | 1, rng.getrandbits(32) | 1))},
                    "stack": [rng.getrandbits(32)]}

        def m_weapon_skill(m, a):
            """FUN_0055c560: the skill's row; its item type (signed word + 0x18) above 0 and is-a 0x30, byte + 0x14 = 2, and usable (FUN_0055c4d0)."""
            node, u = a["regs"]["edi"], a["stack"][0]
            if node == 0:
                return 0
            r = m.call("c644140", node)
            if r == 0:
                return 0
            kind = s32(m.r(r + 0x18, 2) << 16) >> 16
            if kind <= 0 and not mut("type-unsigned"):
                return 0
            if m.call("c629b50", kind, 0x30) == 0 or m.r(r + 0x14, 1) != 2:
                return 0
            return int(m.call("c647960", u, node) not in (2, 7))
        rig.run("FUN_0055c560", 0x55C560, b_weapon_skill, m_weapon_skill, 1)

        def b_same(img, rng):
            """Two items: one on the stack, one in EDI."""
            return {"regs": {"edi": rng.getrandbits(32)}, "stack": [rng.getrandbits(32)]}

        def m_same(m, a):
            """FUN_0055f5d0: for i 0..2 the words of FUN_00627ec0(x, i) and FUN_00627f80(x, i) agree, then FUN_00627f20's."""
            x, y = a["stack"][0], a["regs"]["edi"]
            for i in range(3 if not mut("same-2") else 2):
                if m.call("c627ec0", x, i) & 0xFFFF != m.call("c627ec0", y, i) & 0xFFFF:
                    return 0
                if m.call("c627f80", x, i) & 0xFFFF != m.call("c627f80", y, i) & 0xFFFF:
                    return 0
            return int(m.call("c627f20", x) & 0xFFFF == m.call("c627f20", y) & 0xFFFF)
        rig.run("FUN_0055f5d0", 0x55F5D0, b_same, m_same, 1)

    def snapshot(self, m, u, out):
        """FUN_0055d9d0's writes (EAX unit, ESI out): stats 6, 8, 10; the left (FUN_00620190) and right (FUN_006201d0) skill: FUN_00643ad0 at +0x18 / +0x14 (-1 without one), the skill id FUN_00643ce0 at +0x10 / +0xc (0 without)."""
        for k, stat in enumerate((6, 8, 0xA)):
            m.w(out + 4 * k, m.call("c625480", u, stat, 0))
        for getter, slot_id, slot_owner, line in (("c620190", 0x18, 0x10, 0x9DF), ("c6201d0", 0x14, 0xC, 0x9EB)):
            skill = m.call(getter, u)
            if skill:
                m.w(out + slot_id, m.call("c643ad0", skill))
                m.w(out + slot_owner, m.call("c643ce0", skill, ITEMMODE_CPP, line))
            else:
                m.w(out + slot_id, M32)
                m.w(out + slot_owner, 0 if not mut("owner-1") else 1)

    def back_to_cell(self, m, game, player, item):
        """FUN_0055ed30: flag 2 cleared; the inventory must give this item back (fatal 0x12e9 / 0x12ea); its cell; FUN_00557fd0."""
        if item:
            m.w(item + 0xC4, m.r(item + 0xC4) & ~2)
        inv = m.r(player + 0x60)
        got = m.call("c63ad90", inv, item)
        if got == 0:
            raise Halt(0x12E9)
        if got != item:
            raise Halt(0x12EA)
        kind = s32(m.r(item))
        if kind == 2 or 3 < kind <= 5:
            cell = m.r(m.r(item + 0x2C) + 0xC)
        else:
            path = m.r(item + 0x2C)
            cell = m.call("c6488c0", path) if path else 0
        m.call("c63c550", inv, cell)
        m.call("c557fd0", game, item)

    def mode_moves(self):
        """ItemMode.cpp: snapshots, refresh, quantity, skill, cursor, cell, timers."""
        rig, at = self.rig, self.at

        def m_snapshot(m, a):
            """FUN_0055d9d0 alone."""
            self.snapshot(m, a["regs"]["eax"], a["regs"]["esi"])
        rig.run("FUN_0055d9d0", 0x55D9D0, lambda i, r: {"regs": {"eax": r.getrandbits(32), "esi": at(OUT)}}, m_snapshot, 0)

        def b_refresh(img, rng):
            """Unit in EDX (may be null), an inventory (or none) of 0..4 nodes whose items are type 4 or not."""
            put(img, 0x60, rng.choice((0, at(INV), at(INV))))
            n = rng.choice((0, 1, 2, 4))
            nodes = [at(NODES + 0x40 * i) for i in range(n)]
            put(img, INV + 0x80, nodes[0] if nodes else 0)
            for i in range(n):
                base = NODES + 0x40 * i
                put(img, base + 0x10, nodes[i + 1] if i + 1 < n else 0)
                child = 0x3E00 + 0x20 * i
                put(img, base + 0x14, rng.choice((0, at(child), at(child))))
                put(img, child, rng.choice((4, 4, 2)))
            return {"edx": rng.choice((0, at(0), at(0), at(0))), "ecx": rng.getrandbits(32)}

        def m_refresh(m, a):
            """FUN_0055f500: snapshot; each node's item (taken before the next) of type 4 with FUN_00625820 nonzero: FUN_006277f0, FUN_00627910(its value); then FUN_0055da70."""
            u = a["edx"]
            if u == 0 or m.r(u + 0x60) == 0:
                return None
            snap = bytearray(0x1C)
            m.regions.append((0x7000_0000, snap))
            self.snapshot(m, u, 0x7000_0000)
            m.regions.pop()
            node = m.call("c63b2c0", m.r(u + 0x60))
            while node:
                nxt = m.call("c63dfa0", node)
                child = m.call("c63dfd0", node)
                if child and m.r(child) == 4 and m.call("c625820", child):
                    m.call("c6277f0", u, child)
                    m.call("c627910", u, child, mix("c625820v", child))
                node = nxt
            m.call("c55da70", u, bytes(snap) if not mut("snap-zero") else bytes(0x1C))
            return None
        rig.run("FUN_0055f500", 0x55F500, b_refresh, m_refresh, 0)

        def m_quantity(m, a):
            """FUN_0055f6e0: quantity set (FUN_006272b0), read back, the client told (line 0x18e0, FUN_0053d130), FUN_0055c070."""
            game, (item, value) = a["edx"], a["stack"]
            m.call("c6272b0", item, 0x46, value, 0)
            q = m.call("c625480", item, 0x46, 0)
            client = m.call("c5531c0", game, ITEMMODE_CPP, 0x18E0)
            m.call("c53d130", client, item, 1, 0x46, q, 0 if not mut("tell-arg") else 1)
            m.call("c55c070", item, value)
        rig.run("FUN_0055f6e0", 0x55F6E0, lambda i, r: {"ecx": r.getrandbits(32), "edx": r.getrandbits(32),
                "stack": [r.getrandbits(32), r.getrandbits(32)]}, m_quantity, 2)

        def b_zero(img, rng):
            """Player in ECX (may be null; type +0, id +0xc), item in EDX."""
            put(img, PLAYER, rng.choice((0, 1, 0x1FF)))
            return {"ecx": rng.choice((0, at(PLAYER), at(PLAYER))), "edx": rng.getrandbits(32)}

        def m_zero(m, a):
            """FUN_0055f9c0: the item's book skill (FUN_0055bff0, ESI); unless -1, the skill node's count to 0 and the client told (line 0x1962)."""
            p, item = a["ecx"], a["edx"]
            skill = m.call("c55bff0", item)
            if skill == M32:
                return None
            m.call("c625480", item, 0x46, 0)
            node = m.call("c6439b0", p, skill, M32)
            if node:
                m.call("c6450f0", node)
            m.call("c645120", node, 0)
            kind, uid = (m.r(p), m.r(p + 0xC)) if p else (6, M32)
            client = m.call("c5531c0", p, ITEMMODE_CPP, 0x1962)
            m.call("c53c520", client, kind & 0xFF, uid, skill, 0)
            return None
        rig.run("FUN_0055f9c0", 0x55F9C0, b_zero, m_zero, 0)

        def b_cursor(img, rng):
            """Player in EDX (inventory at + 0x60), item on the stack; either may be null."""
            put(img, PLAYER + 0x60, rng.getrandbits(32))
            return {"ecx": rng.getrandbits(32), "edx": rng.choice((0, at(PLAYER), at(PLAYER))),
                    "stack": [rng.choice((0, rng.getrandbits(32) | 1, rng.getrandbits(32) | 1))]}

        def m_cursor(m, a):
            """FUN_0055fb10: fatal 0x19a0 / 0x19a1; mode 4, flag 0x100000, onto the changed list, the player refreshed; 1."""
            p, item = a["edx"], a["stack"][0]
            if p == 0:
                raise Halt(0x19A0)
            if item == 0:
                raise Halt(0x19A1)
            m.call("c624690", item, 4 if not mut("cursor-mode") else 3)
            m.call("c628170", item, 0x100000, 1)
            m.call("c63cc70", m.r(p + 0x60), item)
            m.call("c621000", p, 1)
            return 1
        rig.run("FUN_0055fb10", 0x55FB10, b_cursor, m_cursor, 1)

        def b_cell(img, rng):
            """Game in ECX, player in EDX, item on the stack (may be null) with a type and path; FUN_0063ad90's answer."""
            put(img, PLAYER + 0x60, rng.getrandbits(32))
            kind = rng.choice((0, 1, 2, 3, 4, 5, 6, 0xFFFFFFFF))
            put(img, 0, kind)
            put(img, 0x2C, at(PATH) if kind in (2, 4, 5) else rng.choice((0, at(PATH))))
            item = rng.choice((0, at(0), at(0), at(0)))
            self.pick["cell"] = rng.choice((item, item, item, 0, rng.getrandbits(32)))
            return {"ecx": rng.getrandbits(32), "edx": at(PLAYER), "stack": [item]}

        rig.run("FUN_0055ed30", 0x55ED30, b_cell,
                lambda m, a: self.back_to_cell(m, a["ecx"], a["edx"], a["stack"][0]), 1)

        def m_told(m, a):
            """FUN_00561e70: the client (line 0xf94), FUN_006287b0's dword, FUN_0053eed0(item, it), then FUN_0055ed30."""
            game, player, item = a["ecx"], a["edx"], a["stack"][0]
            client = m.call("c5531c0", player, ITEMMODE_CPP, 0xF94)
            value = m.call("c6287b0", 0x20, 1)
            m.call("c53eed0", client, player, item, value)
            self.back_to_cell(m, game, player, item)
        rig.run("FUN_00561e70", 0x561E70, b_cell, m_told, 1)

        def b_timer3(img, rng):
            """Game in ECX, item in EDX."""
            return {"ecx": rng.getrandbits(32), "edx": rng.getrandbits(32)}

        def timer3(m, game, item):
            """FUN_00562d30: unbroken (FUN_006280a0 0x100 = 0) with durability: a durability point (stat 0x48 by 0xfc); else stackable: quantity (0x46 by 0xfd)."""
            r = m.call("c6280a0", item, 0x100, 0x15A6, ITEMMODE_CPP)
            if r == 0 and m.call("c629930", item):
                top = m.call("c625e00", item)
                r = m.call("c562c40", 0xFC, 0x48, game, top)
                if r:
                    return r
            r = m.call("c6289f0", item)
            if r == 0:
                return 0
            top = m.call("c6295b0", item)
            return m.call("c562c40", 0xFD, 0x46 if not mut("timer-stat") else 0x48, game, top)
        rig.run("FUN_00562d30", 0x562D30, b_timer3, lambda m, a: timer3(m, a["ecx"], a["edx"]), 0)

        def b_dispatch(img, rng):
            """Timer index on the stack (3 and 4 have entries), IsBadCodePtr's answer."""
            n = rng.choice((3, 3, 4, 0, 1, 5))
            rig.bad_code = 1 if TIMERS_TABLE[n] == 0 else rng.choice((0, 0, 0, 1))
            return {"ecx": rng.getrandbits(32), "edx": rng.getrandbits(32), "stack": [n]}

        def m_dispatch(m, a):
            """FUN_00562da0: a bad entry is fatal 0x15ea; else the entry runs with ECX, EDX (3: timer 3; 4: a bare ret)."""
            if rig.bad_code:
                raise Halt(0x15EA)
            if a["stack"][0] == 3:
                return timer3(m, a["ecx"], a["edx"])
            return None
        rig.run("FUN_00562da0", 0x562DA0, b_dispatch, m_dispatch, 1)
        rig.bad_code = 0


TIMERS_TABLE = {0: 0, 1: 0, 2: 0, 3: 0x562D30, 4: 0x55F120, 5: 0}


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 80
    s = Suite(rig)
    s.items_data()
    s.items_rows()
    s.type_rows()
    s.items_stats()
    s.mode_small()
    s.mode_moves()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("type3", "seed-off", "line-1506", "pair-1c", "alloc-c0", "tmog-off", "line-f3f", "sound-off",
                 "type-count", "default-0", "cap-6", "set-unsigned", "max-lt", "child-mode", "book-out-1", "arm-swap",
                 "status-6", "type-unsigned", "same-2", "owner-1", "snap-zero", "tell-arg", "cursor-mode",
                 "timer-stat")
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
    rig_mod.run_suite(suite, MUTATIONS, "ITEMS small function (second set)", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
