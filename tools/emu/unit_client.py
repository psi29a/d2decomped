# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the client-side UNIT\\CUnit.cpp, CUnitEvent.cpp and monster-flag accessors in stock game.exe 1.14d.

    uv run python unit_client.py [--mutation-check]

Runs game.exe's own code on fake client units (a 0x100-byte block of random
bytes with the type and the fields set) and checks each call against a model
written from the decompile and the disassembly: the return value (EAX; a
void routine is only required to return), every byte of the unit and chain
blocks and of the two unit hash tables after each call, the ordered log of
the hooked callees (registers and stack arguments) and the bytes `ret n`
popped. A fatal error (the .\\UNIT\\CUnit.cpp line passed to 0x40_8a60) is a
"halt" with that line; 0x40_8090 itself runs for real.

Unit fields used: +0 type, +4 class, +0xc id, +0x10 mode, +0x38 sequence
frame (<< 8), +0x54 gfx info, +0x58 a small counter, +0x64 light map,
+0x68, +0x78 sound list head, +0x90 event list head, +0xc4 / +0xc8 flag
words, +0xd8, +0xe4 hash chain link. Hash tables: 0x7a_5e70 and 0x7a_5270,
six unit types of 0x80 chain heads each (0x200 bytes a type).

Covered: FUN_00463720, 00463830, 00463900, 00463940, 004639b0, 00463c70,
00463c90, 00463e20, 00463e50, 00463e80, 00463eb0, 004643f0, 00464370,
004645b0, 00464770, 004647a0, 004647d0, 00464810, 00464860, 00464930,
00464950, 00464990, 004649d0, 00464a10, 00464a30, 00464c40, 00464c90,
00464cf0, 00465400, 00465e80, 00465ee0, 00465f00, 004661c0, 004ade40,
004ade50, 004ade60, 004ade70, 004ae080, 004ae110, 004ae320, 004ae340,
004ae360, 004ae380, 004ae3a0, 004ca8a0, 004ca8d0, 004ccf20, 004ccf30,
004dc0c0, 004dc110, 004dc190.

`--mutation-check` breaks the model on purpose, one defect at a time, and
requires each to fail.
"""
import random
import struct
import sys

import _monster_world as mw
from _monster_world import require
from units_small import MASK, Scene, rand32, signed

ASSERT = 0x40_8A60
FILE_CUNIT_EVENT = ".\\UNIT\\CUnitEvent.cpp"
TABLE_A, TABLE_B = 0x7A_5E70, 0x7A_5270
TABLE_SIZE = 0xC00
GLOBAL = 0x744304
FLAG_GLOBAL = 0x72_A348
MUTATIONS = (
    "set68-offset", "d8-get-offset", "d8-set-offset", "state58-not-3", "countdown-by-2", "countdown-inverted",
    "countdown-zero-calls", "tail-edx-zero", "tail-wrong-field", "list78-offset", "flag-wrong-bit", "party-type-1",
    "party-eax-cleared", "walk-type-4", "walk-link-e0", "lightmap-keeps-pointer", "lightmap-always-call",
    "lookup-null-id-zero", "lookup-add-1bd", "selected-type-3", "selected-flag-40", "flag4-type-2",
    "flag4-inverted", "flag4-clear-keeps", "flags-all-mask", "index-negative-ok", "index-bad-zero", "class-word-unsigned",
    "clear-one-table", "mon-data-34", "mon-mask-swapped", "mon-type-2", "global-set-wrong", "dispatch-index-swap",
    "find-no-fatal", "find-table-swap", "remove-keeps-head", "remove-last-return", "remove-type4-no-hook",
    "iter-bucket-127", "iter-reverse", "iter-tables-swapped", "target-index-3f", "target-type-shift-8",
    "warp-null-continues", "warp-args-swapped", "seq-logical-shift", "seq-anim-11", "seq-ready-strict",
    "event-free-skips-last", "event-alloc-pool-0x10", "event-alloc-no-prev-link", "event-alloc-fields-swapped",
    "event-remove-state-ignored", "event-remove-head-keeps", "alive-player-mode-10", "alive-object-bit1",
    "alive-missile-flag", "alive-item-mode-2", "alive-monster-flag",
)
mutation = None
CASES = 0
CALLED = set()
COVERED = (0x46_3720, 0x46_3830, 0x46_3900, 0x46_3940, 0x46_39B0, 0x46_3C70, 0x46_3C90, 0x46_3E20, 0x46_3E50,
           0x46_3E80, 0x46_3EB0, 0x46_43F0, 0x46_4370, 0x46_45B0, 0x46_4770, 0x46_47A0, 0x46_47D0, 0x46_4810,
           0x46_4860, 0x46_4930, 0x46_4950, 0x46_4990, 0x46_49D0, 0x46_4A10, 0x46_4A30, 0x46_4C40, 0x46_4C90,
           0x46_4CF0, 0x46_5400, 0x46_5E80, 0x46_5EE0, 0x46_5F00, 0x46_61C0, 0x4A_DE40, 0x4A_DE50, 0x4A_DE60,
           0x4A_DE70, 0x4A_E080, 0x4A_E110, 0x4A_E320, 0x4A_E340, 0x4A_E360, 0x4A_E380, 0x4A_E3A0, 0x4C_A8A0,
           0x4C_A8D0, 0x4C_CF20, 0x4C_CF30, 0x4D_C0C0, 0x4D_C110, 0x4D_C190)


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


class Halt(Exception):
    """game.exe took the fatal-error path; args[0] is the source line passed to it."""


class Rig(mw.World):
    """The emulated game with the fatal path and every callee of the covered routines hooked."""

    def __init__(self):
        """Hook the assert and the callees; script keys are named after the hook."""
        super().__init__()
        mw.INSTRUCTION_LIMIT = 400000
        self.m.hook(ASSERT, self.fatal, 0)
        lg = self.logger

        def scr(name):
            """Scripted return of the hook called `name`."""
            return lambda c: self.script.get(name, 0)
        for address, name in ((0x46_F1C0, "cof-1c0"), (0x46_F1F0, "cof-1f0"), (0x46_F220, "cof-220"),
                              (0x46_F250, "cof-250"), (0x51_6950, "snd-950"), (0x51_6980, "snd-980"),
                              (0x4A_C7E0, "montype")):
            lg(address, 0, name, ret=scr(name), regs=("ecx", "edx"))
        lg(0x47_9C00, 0, "party", ret=scr("party"), regs=("ecx",))
        lg(0x47_43D0, 0, "free-lightmap", ret=scr("free-lightmap"), regs=("ecx",))
        lg(0x61_A1D0, 2, "room-find", ret=scr("room-find"), regs=())
        lg(0x61_DB70, 1, "room-field", ret=scr("room-field"), regs=())
        lg(0x46_7A10, 0, "selected", ret=scr("selected"), regs=())
        lg(0x64_0E90, 1, "objects-row", ret=scr("objects-row"), regs=())
        lg(0x46_38A0, 0, "monster-flag", ret=scr("monster-flag"), regs=("ecx", "edx"))
        lg(0x48_79D0, 0, "item-unhook", ret=scr("item-unhook"), regs=("ecx",))
        lg(0x46_5870, 0, "free-any", ret=scr("free-any"), regs=("esi", "eax"))
        lg(0x62_0C10, 2, "set-target", ret=scr("set-target"), regs=())
        lg(0x62_0290, 2, "alloc-unit", ret=scr("alloc-unit"), regs=())
        lg(0x46_5FD0, 6, "init-common", ret=scr("init-common"), regs=("eax", "ebx"))
        lg(0x62_0250, 1, "current-skill", ret=scr("current-skill"), regs=())
        lg(0x64_4140, 0, "skill-row", ret=scr("skill-row"), regs=("ecx",))
        lg(0x63_9DF0, 2, "stat-check", ret=scr("stat-check"), regs=())
        lg(0x4D_C0B0, 0, "event-pool", ret=scr("event-pool"), regs=("ecx",))
        lg(0x4A_DD90, 3, "mod-dispatch", ret=scr("mod-dispatch"), regs=())

    def fatal(self, current):
        """0x40_8a60 (message, caller address, line): the line is the third argument."""
        raise Halt(current.arg(2))

    def go(self, address, *args, ecx=0, edx=0, regs=None, nstack=None):
        """One call; ("halt", line) when the fatal path ran, else ("ret", eax)."""
        CALLED.add(address)
        try:
            return ("ret", self.run(address, *args, ecx=ecx, edx=edx, regs=regs, nstack=nstack))
        except Halt as halt:
            return ("halt", halt.args[0])


def verify(rig, label, got, want, want_log, *scenes):
    """Compare the result (want None: a void routine, only "ret"), the callee log and every block."""
    global CASES
    CASES += 1
    if want is None:
        require(got[0] == "ret", f"{label}: game.exe {got}, model returns")
    else:
        require(got == want, f"{label}: game.exe {got}, model {want}")
    require(rig.log == want_log, f"{label}: callee log {rig.log} != {want_log}")
    for sc in scenes:
        sc.check(label)


def unit_of(rig, rng, kind=None, size=0x100):
    """A scene with one random unit block (type `kind`)."""
    sc = Scene(rig, rng)
    sc.unit = sc.block(size)
    if kind is not None:
        sc.put(sc.unit, kind)
    return sc


def halts(rig, label, address, line, *args, ecx=0, edx=0):
    """A null unit makes the routine call the fatal path with `line`; nothing else is called."""
    global CASES
    CASES += 1
    got = rig.go(address, *args, ecx=ecx, edx=edx)
    require(got == ("halt", line), f"{label}: game.exe {got}, model halt {line:#x}")
    require(rig.log == [], f"{label}: unexpected callee {rig.log}")


def check_fields(rig, rng):
    """FUN_00464cf0 (+0x68), 00464770 / 004647a0 (+0xd8 get / set), 00464c90 (+0x58 = 3); null units are fatal."""
    for _ in range(12):
        value = rand32(rng)
        sc = unit_of(rig, rng)
        sc.w(sc.unit + (0x6C if mut("set68-offset") else 0x68), value)
        verify(rig, "set68", rig.go(0x46_4CF0, ecx=sc.unit, edx=value), None, [], sc)
        sc = unit_of(rig, rng)
        want = sc.r(sc.unit + (0xDC if mut("d8-get-offset") else 0xD8))
        verify(rig, "get-d8", rig.go(0x46_4770, ecx=sc.unit), ("ret", want), [], sc)
        sc = unit_of(rig, rng)
        sc.w(sc.unit + (0xDC if mut("d8-set-offset") else 0xD8), value)
        verify(rig, "set-d8", rig.go(0x46_47A0, ecx=sc.unit, edx=value), None, [], sc)
        sc = unit_of(rig, rng)
        sc.w(sc.unit + 0x58, 2 if mut("state58-not-3") else 3)
        verify(rig, "set58", rig.go(0x46_4C90, ecx=sc.unit), None, [], sc)
    for address, line in ((0x46_4CF0, 0xA4A), (0x46_4770, 0x8A5), (0x46_47A0, 0x8AF), (0x46_4C90, 0xA34),
                          (0x46_4C40, 0xA19), (0x46_3E20, 0x563), (0x46_3E50, 0x56C), (0x46_3E80, 0x575),
                          (0x46_3EB0, 0x57E), (0x4C_A8A0, 0xC2C), (0x4C_A8D0, 0xC2C)):
        halts(rig, f"null {address:#x}", address, line, ecx=0, edx=5)


def check_countdown(rig, rng):
    """FUN_00464c40: +0x58 counts down; a non-zero count is decremented and FUN_00639df0(unit, 0x8f) == 0 gives 1."""
    for _ in range(40):
        sc = unit_of(rig, rng)
        count = rng.choice((0, 1, 2, 3, 0xFFFFFFFF, rng.randrange(1 << 32)))
        sc.put(sc.unit + 0x58, count)
        rig.script["stat-check"] = rng.choice((0, 0, 1, rng.randrange(1 << 32)))
        if count == 0 and not mut("countdown-zero-calls"):
            want, log = ("ret", 0), []
        else:
            if count:
                sc.w(sc.unit + 0x58, (count - (2 if mut("countdown-by-2") else 1)) & MASK)
            result = int(rig.script["stat-check"] == 0)
            if mut("countdown-inverted"):
                result ^= 1
            want, log = ("ret", result), [("stat-check", sc.unit, 0x8F)]
        verify(rig, f"countdown {count:#x}", rig.go(0x46_4C40, ecx=sc.unit), want, log, sc)


def check_tails(rig, rng):
    """FUN_00463e20 ... 00463eb0 (unit +0x54 to ECX, EDX kept) and 004ca8a0 / 004ca8d0 (unit +0x78) tail calls."""
    table = ((0x46_3E20, 0x54, "cof-1c0"), (0x46_3E50, 0x54, "cof-1f0"), (0x46_3E80, 0x54, "cof-220"),
             (0x46_3EB0, 0x54, "cof-250"), (0x4C_A8A0, 0x78, "snd-950"), (0x4C_A8D0, 0x78, "snd-980"))
    for address, offset, name in table:
        for _ in range(8):
            sc = unit_of(rig, rng)
            edx = rand32(rng)
            rig.script[name] = rand32(rng)
            use = 0x58 if mut("tail-wrong-field") and offset == 0x54 else (0x7C if mut("list78-offset") else offset)
            if offset == 0x54:
                arg = sc.r(sc.unit + use)
            else:
                arg = (sc.unit + use) & MASK
            shown = 0 if mut("tail-edx-zero") else edx
            verify(rig, f"tail {address:#x}", rig.go(address, ecx=sc.unit, edx=edx),
                   ("ret", rig.script[name]), [(name, arg, shown)], sc)


def check_flags(rig, rng):
    """FUN_00464810 (sets 0x10000 in +0xc4), 00463720 (+0xc8 & mask) and 004647d0 (+0xc4 bit 0x10000 off, bit 4 on)."""
    for _ in range(30):
        sc = unit_of(rig, rng)
        sc.w(sc.unit + 0xC4, sc.r(sc.unit + 0xC4) | (0x20000 if mut("flag-wrong-bit") else 0x10000))
        verify(rig, "set-flag", rig.go(0x46_4810, ecx=sc.unit), None, [], sc)
        verify(rig, "set-flag null", rig.go(0x46_4810, ecx=0), None, [])
        sc = unit_of(rig, rng)
        mask = rand32(rng)
        bits = sc.r(sc.unit + 0xC8)
        hit = (bits & mask) == mask if mut("flags-all-mask") else (bits & mask) != 0
        verify(rig, "flags-any", rig.go(0x46_3720, ecx=sc.unit, edx=mask), ("ret", int(hit)), [], sc)
        verify(rig, "flags-any null", rig.go(0x46_3720, ecx=0, edx=mask), ("ret", 0), [])
        sc = unit_of(rig, rng, rng.choice((0, 1, 1, 2, 3)))
        kind = sc.r(sc.unit)
        sc.put(sc.unit + 4, rng.randrange(1 << 32))
        rig.script["monster-flag"] = rng.choice((0, 0, 1, 5))
        old = sc.r(sc.unit + 0xC4)
        new = old if mut("flag4-clear-keeps") else old & ~0x10000 & MASK
        log = []
        check_type = 2 if mut("flag4-type-2") else 1
        if kind == check_type:
            log = [("monster-flag", sc.r(sc.unit + 4), 9)]
            hit = rig.script["monster-flag"] != 0
            if mut("flag4-inverted"):
                hit = not hit
        else:
            hit = True
        if hit:
            new |= 4
        sc.w(sc.unit + 0xC4, new)
        verify(rig, "flag4", rig.go(0x46_47D0, ecx=sc.unit), None, log, sc)


def check_party(rig, rng):
    """FUN_00465400 (player id lookup or EAX | 0xffff), 00464a10 (first type-5 unit on the +0xe8 chain), 00464a30."""
    for _ in range(40):
        kind = rng.choice((0, 0, 1, 2, 5))
        sc = unit_of(rig, rng, kind)
        rig.script["party"] = rand32(rng)
        eax = rand32(rng)
        if sc.r(sc.unit) == (1 if mut("party-type-1") else 0):
            want, log = ("ret", rig.script["party"]), [("party", sc.unit)]
        else:
            want, log = ("ret", (0 if mut("party-eax-cleared") else eax) | 0xFFFF), []
        verify(rig, "party", rig.go(0x46_5400, ecx=sc.unit, regs={"eax": eax}), want, log, sc)
    verify(rig, "party null", rig.go(0x46_5400, ecx=0, regs={"eax": 0x12345678}), ("ret", 0x1234FFFF), [])
    for _ in range(40):
        sc = Scene(rig, rng)
        length = rng.randrange(0, 6)
        nodes = [sc.block(0x100) for _ in range(length)]
        for i, node in enumerate(nodes):
            sc.put(node, rng.choice((0, 1, 4, 5, 5)))
            sc.put(node + 0xE8, nodes[i + 1] if i + 1 < length else 0)
            sc.put(node + 0xE0, nodes[i + 2] if i + 2 < length else 0)
        want = 0
        walk = 0xE0 if mut("walk-link-e0") else 0xE8
        node = nodes[0] if nodes else 0
        while node:
            if sc.r(node) == (4 if mut("walk-type-4") else 5):
                want = node
                break
            node = sc.r(node + walk)
        verify(rig, "chain walk", rig.go(0x46_4A10, regs={"eax": nodes[0] if nodes else 0}), ("ret", want), [], sc)
    for _ in range(40):
        sc = unit_of(rig, rng, rng.choice((0, 1, 2)))
        ecx = rand32(rng)
        edx = 0 if rng.random() < 0.3 else sc.unit
        rig.script["room-find"] = rng.choice((0, 0, rng.randrange(1, 1 << 32)))
        rig.script["room-field"] = rng.randrange(1 << 32)
        ident = (0 if mut("lookup-null-id-zero") else MASK) if edx == 0 else sc.r(sc.unit + 4)
        log = [("room-find", ecx, ident)]
        want = 0
        if rig.script["room-find"]:
            log.append(("room-field", rig.script["room-find"]))
            want = (rig.script["room-field"] + (0x1BD if mut("lookup-add-1bd") else 0x1BE)) & MASK
        verify(rig, "room lookup", rig.go(0x46_4A30, ecx=ecx, edx=edx), ("ret", want), log, sc)


def check_lightmap(rig, rng):
    """FUN_00464930: a unit's light map (+0x64) is released through 0x47_43d0 (hooked) and cleared."""
    for _ in range(20):
        sc = unit_of(rig, rng)
        pointer = rng.choice((0, 0, rng.randrange(1, 1 << 32)))
        sc.put(sc.unit + 0x64, pointer)
        log = []
        if pointer or mut("lightmap-always-call"):
            log = [("free-lightmap", pointer)]
            if not mut("lightmap-keeps-pointer"):
                sc.w(sc.unit + 0x64, 0)
        verify(rig, "lightmap", rig.go(0x46_4930, ecx=sc.unit), None, log, sc)


def check_selected(rig, rng):
    """FUN_00464370: false when the selected unit is an object whose row flag 0x80 is set, else selected == unit."""
    for _ in range(60):
        sc = Scene(rig, rng)
        sc.unit, sc.sel, sc.row = sc.block(0x100), sc.block(0x100), sc.block(0x200)
        sc.put(sc.sel, rng.choice((0, 1, 2, 2, 3)))
        sc.put(sc.sel + 4, rng.randrange(1 << 32))
        sc.put(sc.row + 0x167, rng.choice((0, 0x80, 0x40, 0xC0, 0x7F)), 1)
        selected = rng.choice((sc.sel, sc.sel, sc.unit, 0))
        rig.script["selected"] = selected
        rig.script["objects-row"] = sc.row
        log = [("selected",)]
        flag = 0x40 if mut("selected-flag-40") else 0x80
        kind = 3 if mut("selected-type-3") else 2
        if selected and sc.r(selected) == kind:
            log.append(("objects-row", sc.r(selected + 4)))
            if sc.b(sc.row + 0x167) & flag:
                verify(rig, "selected", rig.go(0x46_4370, ecx=sc.unit), ("ret", 0), log, sc)
                continue
        log.append(("selected",))
        verify(rig, "selected", rig.go(0x46_4370, ecx=sc.unit), ("ret", int(selected == sc.unit)), log, sc)


def check_class_table(rig, rng):
    """FUN_00463900 (index bound against +0xa80, else -1), 00463830 (word +2 of a 0x1a8-byte row, else the index)."""
    for _ in range(80):
        sc = Scene(rig, rng)
        sc.table, sc.head = sc.block(0x1A8 * 8), sc.block(0xB00)
        count = rng.randrange(0, 9)
        sc.put(sc.head + 0xA80, count)
        sc.put(sc.head + 0xA78, sc.table)
        rig.write(GLOBAL, struct.pack("<I", sc.head))
        index = rng.choice((-3, -1, 0, 1, 2, count - 1, count, count + 1, 7, 0x7FFFFFFF, rng.randrange(-5, 10)))
        arg = index & MASK
        bad = signed(arg) >= count if mut("index-negative-ok") else (signed(arg) < 0 or signed(arg) >= count)
        want = (0 if mut("index-bad-zero") else MASK) if bad else arg
        verify(rig, "class index", rig.go(0x46_3900, ecx=arg), ("ret", want), [], sc)
        if signed(arg) < 0 or signed(arg) >= count:
            want = arg
        else:
            raw = struct.unpack_from("<H", bytes(sc.blocks[sc.table]), 0x1A8 * arg + 2)[0]
            want = (raw if mut("class-word-unsigned") else raw - 0x10000 if raw & 0x8000 else raw) & MASK
        verify(rig, "class word", rig.go(0x46_3830, ecx=arg), ("ret", want), [], sc)
    sc = Scene(rig, rng)
    sc.head = sc.block(0xB00)
    sc.put(sc.head + 0xA80, 3)
    sc.put(sc.head + 0xA78, 0)
    rig.write(GLOBAL, struct.pack("<I", sc.head))
    verify(rig, "class word no table", rig.go(0x46_3830, ecx=0), ("ret", 0), [], sc)


def check_monster(rig, rng):
    """FUN_004ae110 / 004ae080 (monster data +0x30 / word +0x28 & mask), 004ae320 ... 004ae3a0 (type-flag tests)."""
    for _ in range(40):
        sc = Scene(rig, rng)
        sc.unit, sc.data = sc.block(0x100), sc.block(0x80)
        sc.put(sc.unit, rng.choice((0, 1, 1, 1, 2)))
        sc.put(sc.unit + 0x14, rng.choice((sc.data, sc.data, 0)))
        kind = 2 if mut("mon-type-2") else 1
        ok = sc.r(sc.unit) == kind and sc.r(sc.unit + 0x14) != 0
        want = ((sc.r(sc.unit + 0x14) + (0x34 if mut("mon-data-34") else 0x30)) & MASK) if ok else 0
        verify(rig, "mon data", rig.go(0x4A_E110, ecx=sc.unit), ("ret", want), [], sc)
        verify(rig, "mon data null", rig.go(0x4A_E110, ecx=0), ("ret", 0), [])
        mask = rand32(rng)
        want = (struct.unpack_from("<H", bytes(sc.blocks[sc.data]), 0x28)[0] & (mask & 0xFFFF)) if ok else 0
        verify(rig, "mon word", rig.go(0x4A_E080, ecx=sc.unit, edx=mask), ("ret", want), [], sc)
        verify(rig, "mon word null", rig.go(0x4A_E080, ecx=0, edx=mask), ("ret", 0), [])
        for address, flag in ((0x4A_E320, 4), (0x4A_E340, 0x40), (0x4A_E360, 8), (0x4A_E380, 0x10), (0x4A_E3A0, 2)):
            sc2 = unit_of(rig, rng, rng.choice((0, 1, 1, 2)))
            rig.script["montype"] = rand32(rng)
            edx = rand32(rng)
            if mut("mon-mask-swapped") and flag == 8:
                flag = 0x10
            if sc2.r(sc2.unit) == 1:
                want, log = ("ret", rig.script["montype"]), [("montype", sc2.unit, flag)]
            else:
                want, log = ("ret", 0), []
            verify(rig, f"mon type {flag:#x}", rig.go(address, ecx=sc2.unit, edx=edx), want, log, sc2)
            verify(rig, "mon type null", rig.go(address, ecx=0, edx=edx), ("ret", 0), [])


def check_globals(rig, rng):
    """FUN_004ccf20 / 004ccf30 (store / read the dword 0x72_a348) and the monster modifier wrappers 004ade40 ... 004ade70."""
    for _ in range(10):
        value = rand32(rng)
        rig.go(0x4C_CF20, ecx=value)
        require(rig.m.r32(FLAG_GLOBAL) == (0 if mut("global-set-wrong") else value), "global store")
        rig.m.w32(FLAG_GLOBAL, value ^ 0x5A5A)
        verify(rig, "global read", rig.go(0x4C_CF30), ("ret", value ^ 0x5A5A), [])
    for address, index in ((0x4A_DE40, 0), (0x4A_DE50, 1), (0x4A_DE60, 2), (0x4A_DE70, 3)):
        sc = unit_of(rig, rng)
        rig.script["mod-dispatch"] = rand32(rng)
        shown = {1: 0, 0: 1}.get(index, index) if mut("dispatch-index-swap") else index
        verify(rig, f"dispatch {index}", rig.go(address, ecx=sc.unit), ("ret", rig.script["mod-dispatch"]),
               [("mod-dispatch", sc.unit, 0, shown)], sc)


def clear_tables(rig, rng):
    """Random chain heads in both hash tables; the model copies start equal."""
    tables = {}
    for base in (TABLE_A, TABLE_B):
        data = bytearray(rng.randbytes(TABLE_SIZE))
        rig.write(base, data)
        tables[base] = data
    return tables


def check_tables(rig, rng):
    """FUN_00463c70 zeroes both 0xc00-byte unit hash tables and leaves the bytes around them alone."""
    for _ in range(4):
        tables = clear_tables(rig, rng)
        edges = {TABLE_A + TABLE_SIZE: 64, TABLE_B - 64: 64}
        before = {a: rig.m.read(a, n) for a, n in edges.items()}
        rig.go(0x46_3C70)
        for base, data in tables.items():
            if mut("clear-one-table") and base == TABLE_B:
                want = bytes(data)
            else:
                want = bytes(TABLE_SIZE)
            require(rig.m.read(base, TABLE_SIZE) == want, f"tables cleared at {base:#x}")
        for address, size in edges.items():
            require(rig.m.read(address, size) == before[address], f"tables spill at {address:#x}")


def chain(sc, rng, table, kind, bucket, length):
    """A chain of `length` nodes in table[kind][bucket]; ids of the bucket (id & 0x7f == bucket), type per node."""
    nodes = [sc.block(0x100) for _ in range(length)]
    for i, node in enumerate(nodes):
        sc.put(node, kind if rng.random() < 0.85 else rng.randrange(6))
        sc.put(node + 0xC, (rng.randrange(1 << 20) << 7 | bucket) & MASK)
        sc.put(node + 0xE4, nodes[i + 1] if i + 1 < length else 0)
    table[kind * 0x200 + bucket * 4: kind * 0x200 + bucket * 4 + 4] = struct.pack("<I", nodes[0] if nodes else 0)
    return nodes


def populate(rig, sc, rng, base):
    """Random content for one table: buckets 0..127 of types 0..5 mostly empty, a few chains. Returns bytearray."""
    table = bytearray(TABLE_SIZE)
    sc.nodes = getattr(sc, "nodes", [])
    for _ in range(rng.randrange(1, 14)):
        kind, bucket = rng.randrange(6), rng.choice((0, 1, 5, 127, rng.randrange(128)))
        if struct.unpack_from("<I", table, kind * 0x200 + bucket * 4)[0]:
            continue
        sc.nodes += chain(sc, rng, table, kind, bucket, rng.randrange(1, 5))
    rig.write(base, table)
    return table


def pick(sc, rng, tables, base):
    """A (type, bucket, id) that often names a real chain node of `base`; the node's type is sometimes changed."""
    heads = [(k, b) for k in range(6) for b in range(128)
             if struct.unpack_from("<I", tables[base], k * 0x200 + b * 4)[0]]
    if not heads or rng.random() < 0.2:
        bucket = rng.randrange(128)
        return rng.randrange(6), bucket, rng.randrange(1 << 20) << 7 | bucket
    kind, bucket = rng.choice(heads)
    nodes, node = [], struct.unpack_from("<I", tables[base], kind * 0x200 + bucket * 4)[0]
    while node:
        nodes.append(node)
        node = sc.r(node + 0xE4)
    node = rng.choice(nodes)
    if rng.random() < 0.3:
        sc.put(node, (kind + 1 + rng.randrange(5)) % 6)
    return kind, bucket, sc.r(node + 0xC)


def check_find(rig, rng):
    """FUN_00463940 (EAX bucket, EDX table, ECX id, stack type: the first id match, 0x5a when its type differs), 004639b0."""
    for _ in range(80):
        sc = Scene(rig, rng)
        tables = {TABLE_A: populate(rig, sc, rng, TABLE_A), TABLE_B: populate(rig, sc, rng, TABLE_B)}
        base = rng.choice((TABLE_A, TABLE_B))
        kind, bucket, guid = pick(sc, rng, tables, base)
        slot = base + kind * 0x200
        head = struct.unpack_from("<I", tables[base], kind * 0x200 + bucket * 4)[0]
        node = head
        while node and sc.r(node + 0xC) != guid:
            node = sc.r(node + 0xE4)
        if not node:
            want = ("ret", 0)
        elif sc.r(node) == kind or mut("find-no-fatal"):
            want = ("ret", node)
        else:
            want = ("halt", 0x5A)
        verify(rig, "find", rig.go(0x46_3940, kind, ecx=guid, edx=slot, regs={"eax": bucket}), want, [], sc)
        table = TABLE_B if not mut("find-table-swap") else TABLE_A
        kind, bucket, guid = pick(sc, rng, tables, TABLE_B)
        node = struct.unpack_from("<I", tables[table], kind * 0x200 + (guid & 0x7F) * 4)[0]
        while node and sc.r(node + 0xC) != guid:
            node = sc.r(node + 0xE4)
        want = ("ret", 0) if not node else (("ret", node) if sc.r(node) == kind else ("halt", 0x5A))
        verify(rig, "find client table", rig.go(0x46_39B0, ecx=guid, edx=kind, nstack=0), want, [], sc)


def check_remove(rig, rng):
    """FUN_00465e80 (ECX bucket, EDX table, EDI type, EBX id), 00465ee0 / 00465f00 (id and type to those, table A / B)."""
    for entry in (0x46_5E80, 0x46_5EE0, 0x46_5F00):
        for _ in range(60):
            sc = Scene(rig, rng)
            tables = {TABLE_A: populate(rig, sc, rng, TABLE_A), TABLE_B: populate(rig, sc, rng, TABLE_B)}
            base = rng.choice((TABLE_A, TABLE_B))
            if entry != 0x46_5E80:
                base = TABLE_A if entry == 0x46_5EE0 else TABLE_B
            kind, bucket, guid = pick(sc, rng, tables, base)
            rig.script["free-any"] = rand32(rng)
            rig.script["item-unhook"] = rand32(rng)
            tbl = tables[base]
            slot = kind * 0x200 + bucket * 4
            cur = struct.unpack_from("<I", tbl, slot)[0]
            prev, found = 0, 0
            while cur:
                if sc.r(cur + 0xC) == guid and sc.r(cur) == kind:
                    found = cur
                    break
                prev, cur = cur, sc.r(cur + 0xE4)
            log = []
            if not found:
                want = ("ret", prev)
                if mut("remove-last-return"):
                    want = ("ret", 0)
            else:
                nxt = sc.r(found + 0xE4)
                if prev:
                    sc.w(prev + 0xE4, nxt)
                elif not mut("remove-keeps-head"):
                    struct.pack_into("<I", tbl, slot, nxt)
                eax = prev if prev else nxt
                if kind == 4 and not mut("remove-type4-no-hook"):
                    log.append(("item-unhook", found))
                    eax = rig.script["item-unhook"]
                log.append(("free-any", found, eax))
                want = ("ret", rig.script["free-any"])
            table_arg = base + kind * 0x200
            if entry == 0x46_5E80:
                got = rig.go(entry, ecx=bucket, edx=table_arg, regs={"edi": kind, "ebx": guid}, nstack=0)
            else:
                got = rig.go(entry, ecx=guid, edx=kind, nstack=0)
            verify(rig, f"remove {entry:#x}", got, want, log, sc)
            for b, t in tables.items():
                require(rig.m.read(b, TABLE_SIZE) == bytes(t), f"remove {entry:#x}: table {b:#x} differs")


def check_iterate(rig, rng):
    """FUN_00463c90 (stack table, EBX callback; every node of the 0x80 buckets, next read first) and its wrappers."""
    for entry, line, base in ((0x46_3C90, None, None), (0x46_4950, 0x961, TABLE_A), (0x46_4990, 0x96C, TABLE_A + 0x200),
                              (0x46_49D0, 0x977, TABLE_A + 0x400)):
        for _ in range(25):
            sc = Scene(rig, rng)
            table = populate(rig, sc, rng, TABLE_A)
            seen = []
            callback = rig.slot(lambda c, log=seen: log.append(c.mu.reg_read(mw.UC_X86_REG_ECX)) or 0, 0)
            start = base if base else TABLE_A + rng.randrange(6) * 0x200
            if mut("iter-tables-swapped") and base:
                start = TABLE_A if base != TABLE_A else TABLE_A + 0x200
            want = []
            buckets = range(0x7F, -1, -1) if mut("iter-reverse") else range(0x7F if mut("iter-bucket-127") else 0x80)
            for bucket in buckets:
                node = struct.unpack_from("<I", table, start - TABLE_A + bucket * 4)[0]
                while node:
                    want.append(node)
                    node = sc.r(node + 0xE4)
            if base:
                got = rig.go(entry, ecx=callback, nstack=0)
            else:
                got = rig.go(entry, start, regs={"ebx": callback})
            require(got[0] == "ret", f"iterate {entry:#x}: {got}")
            require(seen == want, f"iterate {entry:#x}: callbacks {seen} != {want}")
            verify(rig, "iterate", got, None, [], sc)
        if line:
            halts(rig, f"null {entry:#x}", entry, line, ecx=0)


def check_target(rig, rng):
    """FUN_004643f0 (unit, ECX; type EDX; stack id: FindUnit in table A, then FUN_00620c10) and FUN_004661c0 (warp)."""
    for _ in range(60):
        sc = Scene(rig, rng)
        sc.unit = sc.block(0x100)
        tables = {TABLE_A: populate(rig, sc, rng, TABLE_A), TABLE_B: populate(rig, sc, rng, TABLE_B)}
        kind, bucket, guid = pick(sc, rng, tables, TABLE_A)
        rig.script["set-target"] = rand32(rng)
        index = guid & (0x3F if mut("target-index-3f") else 0x7F)
        shift = 8 if mut("target-type-shift-8") else 9
        slot = TABLE_A + kind * (1 << shift)
        node = 0
        if shift == 9:
            node = struct.unpack_from("<I", tables[TABLE_A], kind * 0x200 + index * 4)[0]
        else:
            node = struct.unpack_from("<I", tables[TABLE_A], (slot - TABLE_A) + index * 4)[0] if slot - TABLE_A < TABLE_SIZE - 0x200 else 0
        while node and sc.r(node + 0xC) != guid:
            node = sc.r(node + 0xE4)
        if node and sc.r(node) != kind:
            want, log = ("halt", 0x5A), []
        else:
            want, log = ("ret", rig.script["set-target"]), [("set-target", sc.unit, node)]
        verify(rig, "set target", rig.go(0x46_43F0, guid, ecx=sc.unit, edx=kind), want, log, sc)
    for _ in range(40):
        a = [rand32(rng) for _ in range(6)]
        rig.script["alloc-unit"] = rng.choice((0, rng.randrange(1, 1 << 32)))
        rig.script["init-common"] = rand32(rng)
        if rig.script["alloc-unit"] == 0 and not mut("warp-null-continues"):
            want, log = ("ret", 0), [("alloc-unit", 0, a[4])]
        else:
            want = ("ret", rig.script["init-common"])
            # stock: ECX / EDX are args 0 / 1, stack a[2] a[3] a[4] a[5] (a[0] id in EBX), then 0
            log = [("alloc-unit", 0, a[4]), ("init-common", rig.script["alloc-unit"], a[2], a[0], a[1], a[3], a[4], a[5], 0)]
            if mut("warp-args-swapped"):
                log[1] = ("init-common", rig.script["alloc-unit"], a[2], a[1], a[0], a[3], a[4], a[5], 0)
        got = rig.go(0x46_61C0, a[2], a[3], a[4], a[5], ecx=a[0], edx=a[1], regs={"ebx": 0x1234})
        verify(rig, "warp", got, want, log)


def check_sequence(rig, rng):
    """FUN_004645b0 (unit in ESI): sequence input ready when the skill has no row, another anim, or frame >> 8 >= seqinput."""
    for _ in range(80):
        sc = Scene(rig, rng)
        sc.unit, sc.row = sc.block(0x100), sc.block(0x40)
        sc.put(sc.unit + 0x38, rand32(rng))
        sc.put(sc.row + 0x10, rng.choice((0x12, 0x12, 0x11, 0)), 1)
        sc.put(sc.row + 0x16, rng.choice((0, 0xFF, 1, 7, 0x40, 0x80, rng.randrange(256))), 1)
        rig.script["current-skill"] = rng.choice((0, 5))
        rig.script["skill-row"] = rng.choice((0, sc.row, sc.row))
        log = [("current-skill", sc.unit)]
        if not rig.script["current-skill"]:
            want = 1
        else:
            log.append(("skill-row", 5))
            if not rig.script["skill-row"]:
                want = 0
            elif sc.b(sc.row + 0x10) != (0x11 if mut("seq-anim-11") else 0x12):
                want = 1
            else:
                need = sc.b(sc.row + 0x16)
                frame = signed(sc.r(sc.unit + 0x38))
                frame = (frame & MASK) >> 8 if mut("seq-logical-shift") else frame >> 8
                if need in (0, 0xFF):
                    want = 0
                else:
                    want = int(frame > need if mut("seq-ready-strict") else frame >= need)
        verify(rig, "sequence", rig.go(0x46_45B0, 0, regs={"esi": sc.unit}), ("ret", want), log, sc)


def check_alive(rig, rng):
    """FUN_00464860: the unit is dead or dying by type (player mode 0x11, monster 0xc, object flags, missile, item mode 3)."""
    for _ in range(200):
        kind = rng.choice((0, 1, 2, 3, 4, 5, 6))
        sc = Scene(rig, rng)
        sc.unit, sc.row = sc.block(0x100), sc.block(0x200)
        sc.put(sc.unit, kind)
        sc.put(sc.unit + 0x10, rng.choice((0x11, 0x10, 0xC, 2, 3, 0, 1, rng.randrange(0x20))))
        sc.put(sc.unit + 0xC4, rng.choice((0, 0x10000, rng.randrange(1 << 32))))
        rig.script["monster-flag"] = rng.choice((0, 1))
        rig.script["objects-row"] = sc.row
        sc.put(sc.row + 0x1B7, rng.randrange(256), 1)
        mode, log = sc.r(sc.unit + 0x10), []
        flags = sc.r(sc.unit + 0xC4)
        if kind == 0:
            want = int(mode == (0x10 if mut("alive-player-mode-10") else 0x11))
        elif kind == 1:
            log = [("monster-flag", sc.r(sc.unit + 4), 0x14)]
            want = int(rig.script["monster-flag"] == 0 and mode == 0xC)
            if mut("alive-monster-flag"):
                want = int(rig.script["monster-flag"] != 0 and mode == 0xC)
        elif kind == 2:
            log = [("objects-row", sc.r(sc.unit + 4))]
            bits = sc.b(sc.row + 0x1B7)
            want = int(bool(bits & (1 if mut("alive-object-bit1") else 2)) or (mode == 2 and bool(bits & 1)))
        elif kind == 3:
            want = int(bool(flags & (0x20000 if mut("alive-missile-flag") else 0x10000)))
        elif kind == 4:
            want = int(mode == (2 if mut("alive-item-mode-2") else 3))
        else:
            want = 0
        verify(rig, f"alive type {kind}", rig.go(0x46_4860, ecx=sc.unit), ("ret", want), log, sc)
    verify(rig, "alive null", rig.go(0x46_4860, ecx=0), ("ret", 0), [])


def check_events(rig, rng):
    """FUN_004dc110 (create + link), 004dc190 (remove by type and state), 004dc0c0 (free the list), pool calls logged."""
    file = FILE_CUNIT_EVENT
    for _ in range(40):
        sc = unit_of(rig, rng)
        sc.put(sc.unit + 8, rand32(rng))
        sc.put(sc.unit + 0x90, 0)
        mem, head = {}, 0
        for _ in range(rng.randrange(0, 6)):
            a = [rand32(rng) if rng.random() < 0.5 else rng.randrange(3) for _ in range(5)]
            got = rig.go(0x4D_C110, *a[1:5], 0x77, ecx=sc.unit, edx=a[0])
            require(got[0] == "ret", "event alloc returns")
            node = got[1]
            pool = (sc.r(sc.unit + 8) + (0x10 if mut("event-alloc-pool-0x10") else 0)) & MASK
            require(rig.log == [("alloc", pool, 0x20, file, 0x48, 0)], f"event alloc log {rig.log}")
            # +0 edx, +4 stack3, +8 stack4, +0xc stack0, +0x10 stack1, +0x14 stack2, +0x18 0, +0x1c old head
            fields = [a[0], a[4], 0x77, a[1], a[2], a[3], 0, head]
            if mut("event-alloc-fields-swapped"):
                fields[3], fields[4] = fields[4], fields[3]
            mem[node] = bytearray(struct.pack("<8I", *fields))
            if head and not mut("event-alloc-no-prev-link"):
                struct.pack_into("<I", mem[head], 0x18, node)
            head = node
            CASES_BUMP()
            for address, data in mem.items():
                require(rig.m.read(address, 0x20) == bytes(data), f"event node {address:#x} differs after alloc")
            require(rig.m.r32(sc.unit + 0x90) == head, "event head after alloc")
        rig.script["event-pool"] = rand32(rng)
        if mem:
            ordered, cur = [], head
            while cur:
                ordered.append(cur)
                cur = struct.unpack_from("<I", mem[cur], 0x1C)[0]
            target = mem[rng.choice(ordered)]
            etype, estate = struct.unpack_from("<II", target, 4)
            if rng.random() < 0.3:
                estate ^= 1
            want_log, cur = [], head
            while cur:
                nxt = struct.unpack_from("<I", mem[cur], 0x1C)[0]
                this_type, this_state = struct.unpack_from("<II", mem[cur], 4)
                if this_type == etype and (mut("event-remove-state-ignored") or this_state == estate):
                    prev = struct.unpack_from("<I", mem[cur], 0x18)[0]
                    if prev:
                        struct.pack_into("<I", mem[prev], 0x1C, nxt)
                    elif not mut("event-remove-head-keeps"):
                        head = nxt
                    if nxt:
                        struct.pack_into("<I", mem[nxt], 0x18, prev)
                    want_log += [("event-pool", sc.unit), ("free", rig.script["event-pool"], cur, file, 0x80, 0)]
                cur = nxt
            got = rig.go(0x4D_C190, estate, ecx=sc.unit, edx=etype)
            require(got[0] == "ret" and rig.log == want_log, f"event remove {got} {rig.log} != {want_log}")
            require(rig.m.r32(sc.unit + 0x90) == head, "event head after remove")
            for address, data in mem.items():
                require(rig.m.read(address, 0x20) == bytes(data), f"event node {address:#x} differs after remove")
            CASES_BUMP()
        order, cur = [], rig.m.r32(sc.unit + 0x90)
        while cur and len(order) < 20:
            order.append(cur)
            cur = struct.unpack_from("<I", mem[cur], 0x1C)[0]
        rig.script["event-pool"] = rand32(rng)
        want_log = []
        for n in (order[:-1] if mut("event-free-skips-last") and order else order):
            want_log += [("event-pool", sc.unit), ("free", rig.script["event-pool"], n, file, 0x2F, 0)]
        got = rig.go(0x4D_C0C0, ecx=sc.unit)
        require(got[0] == "ret" and rig.log == want_log, f"event free {rig.log} != {want_log}")
        require(rig.m.r32(sc.unit + 0x90) == 0, "event list cleared")
        CASES_BUMP()
        verify(rig, "event free null", rig.go(0x4D_C0C0, ecx=0), None, [])
        verify(rig, "event remove null", rig.go(0x4D_C190, 3, ecx=0, edx=1), None, [])
        verify(rig, "event alloc null", rig.go(0x4D_C110, 1, 2, 3, 4, 5, ecx=0, edx=7), ("ret", 0), [])


def CASES_BUMP():
    """Count a case checked outside verify()."""
    global CASES
    CASES += 1


def run_all():
    """Run every check; returns the case count."""
    global CASES
    CASES = 0
    CALLED.clear()
    rng = random.Random(20261110)
    rig = Rig()
    rig.m.w32(GLOBAL, 0)
    for check, rounds in ((check_fields, 2), (check_countdown, 2), (check_tails, 2), (check_flags, 2), (check_party, 2),
                          (check_lightmap, 2), (check_selected, 2), (check_class_table, 2), (check_monster, 2),
                          (check_globals, 2), (check_tables, 1), (check_find, 3), (check_remove, 2),
                          (check_iterate, 2), (check_target, 2), (check_sequence, 2), (check_alive, 2),
                          (check_events, 3)):
        for _ in range(rounds):
            check(rig, rng)
    missing = [f"{a:#x}" for a in COVERED if a not in CALLED]
    require(not missing, f"functions never called: {missing}")
    return CASES


def main():
    """Command line entry."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "client UNIT cases")


if __name__ == "__main__":
    main()
