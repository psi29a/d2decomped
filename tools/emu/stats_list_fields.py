# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the stat-list field accessors, wrappers and named-stat getters in stock game.exe 1.14d.

    uv run python stats_list_fields.py [--mutation-check]

Runs game.exe's own code (Stats\\StatsEx.cpp) against a hand-written model on
random stat lists (see _stats_world.py) and compares the return value, the
callee log (arguments in call order), every list's fields and arrays, the
memory a function writes and the bytes the `ret n` pops.

Claimed (stdcall, args on the stack, unless a register is named):
  FUN_00625250 list type, FUN_00625270 plain count, FUN_00625290 expanded count,
  FUN_006252b0 list +0xc, FUN_006252f0 list +0x14, FUN_00626090 list +0x18,
  FUN_006260d0/006260e0 set/get list +0x1c, FUN_006260f0/00626100 set/get +0x20,
  FUN_00625620 value of plain entry n, FUN_00625c90 copy the first n plain entries,
  FUN_00625820 parent list's owner, FUN_00625a00/00625a20 set/test flag 0x100,
  FUN_00625bb0 / FUN_00625be0 the +0x58 pointer and the bitmap after it,
  FUN_00625960 free the changed-stat queue, FUN_00626d10 drop a unit's list,
  the wrappers FUN_006254e0, 006254f0, 00625520, 006255a0, 00625600, 00625a50,
  00626070 and the named-stat getters FUN_00625e60, 00625ef0, 00625f80,
  00625fd0, 00626020 (stats 22/24, 21/23, 160, 159, 31).

Callees are hooked and logged, not claimed: the stat getters 0x62_5350,
0x62_5420, 0x62_5480, 0x62_4f60, the percent helper 0x48_3360, the list free
0x62_6c00 and the allocator. `--mutation-check` breaks the model on purpose,
one defect at a time, and requires each to fail. No MPQs are read.
"""
import argparse
import random
import struct
import sys
import zlib

from unicorn.x86_const import UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EDX

import _stats_world as sw
from _stats_world import EXPANDED, L, Mismatch, normal_entries, require

LIST_TYPE, PLAIN_COUNT, EXP_COUNT = 0x625250, 0x625270, 0x625290
LIST_C, LIST_14, LIST_18 = 0x6252B0, 0x6252F0, 0x626090
SET_1C, GET_1C, SET_20, GET_20 = 0x6260D0, 0x6260E0, 0x6260F0, 0x626100
VALUE_AT, COPY_PLAIN, PARENT_OWNER = 0x625620, 0x625C90, 0x625820
SET_100, TEST_100, EXP_PTR, BITMAP = 0x625A00, 0x625A20, 0x625BB0, 0x625BE0
FREE_QUEUE, DROP_LIST = 0x625960, 0x626D10
THUNK_PLAIN, THUNK_EXP, DIFF, BLEND, UNIT_BLEND, PLAIN_AT, UNIT_EXP = (
    0x6254E0, 0x6254F0, 0x625520, 0x6255A0, 0x625600, 0x625A50, 0x626070)
NAMED = {0x625E60: (22, 24), 0x625EF0: (21, 23), 0x625F80: (160, 160), 0x625FD0: (159, 159), 0x626020: (31, 31)}
# Hooked callees (not claims).
GET_PLAIN, GET_EXP, GET_UNIT, GET_ROW, PERCENT, RELEASE = 0x62_5350, 0x62_5420, 0x62_5480, 0x62_4F60, 0x48_3360, 0x62_6C00
HOOK_KIND = {GET_PLAIN: 1, GET_EXP: 2, GET_UNIT: 3}
MUTATIONS = ("type-null-zero", "index-le", "flag-bit", "bitmap-round-down", "which-swap", "copy-ignores-n",
             "diff-reversed", "owner-flag-inverted")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def mix(*values):
    """A deterministic 32-bit value standing in for a hooked callee's result."""
    return zlib.crc32(struct.pack(f"<{len(values)}I", *[v & 0xFFFFFFFF for v in values]))


class Rig:
    """The machine, the hooks and the case builders for this oracle."""

    def __init__(self):
        """Hook the stat getters, the row lookup, the percent helper and the list release."""
        self.w = world = sw.World()
        self.cases = 0
        machine = world.m
        for address in HOOK_KIND:
            machine.hook(address, self.getter(address), 3)
        machine.hook(GET_ROW, self.row_lookup, 1)
        machine.hook(PERCENT, self.percent, 1)
        machine.hook(RELEASE, self.release, 0)

    def getter(self, address):
        """Stat getter stand-in: log (kind, list, stat, layer) and return a mixed value."""
        def hook(current):
            """Record the three stack arguments."""
            args = tuple(current.arg(i) for i in range(3))
            self.w.log.append(("get", HOOK_KIND[address]) + args)
            return mix(HOOK_KIND[address], *args)
        return hook

    def row_lookup(self, current):
        """Row-based list lookup stand-in (EBX list, EDI key, stack row): log and return a mixed value."""
        regs = (current.mu.reg_read(UC_X86_REG_EBX), current.mu.reg_read(UC_X86_REG_EDI), current.arg(0))
        self.w.log.append(("row",) + regs)
        return mix(9, *regs)

    def percent(self, current):
        """Percent helper stand-in (ECX, EDX, stack): log and return a mixed value."""
        regs = (current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX), current.arg(0))
        self.w.log.append(("percent",) + regs)
        return mix(8, *regs)

    def release(self, current):
        """List release stand-in (ECX list): log it."""
        self.w.log.append(("release", current.mu.reg_read(UC_X86_REG_ECX)))
        return 0

    def make_list(self, rng, flags=None, count=None):
        """Install a random list; returns its address."""
        image = L()
        for offset in (0x00, 0x08, 0x0C, 0x14, 0x18, 0x1C, 0x20, 0x58, 0x5C, 0x60):
            image.f[offset] = rng.randrange(1 << 32)
        image.f[0x10] = rng.randrange(1 << 32) if flags is None else flags
        for name in ("plain", "exp", "queue"):
            n = rng.randrange(0, 7) if count is None else count
            image.arr[name] = normal_entries(rng, n) if name != "queue" else sorted(rng.sample(range(0, 1 << 20, 0x10000), n))
            image.cap[name] = n + rng.choice((0, 0, 2, 4))
        return self.w.install(image)

    def make_unit(self, list_address):
        """A unit record whose +0x5c is `list_address`."""
        unit = self.w.m.alloc(0x120)
        self.w.m.w32(unit + 0x5C, list_address)
        return unit

    def expect(self, name, address, args, value, log, nstack, **kwargs):
        """Run one stock call and compare return value, log, lists and popped bytes."""
        got, _ = self.w.run(address, *args, nstack=nstack, **kwargs)
        self.cases += 1
        require(got == value & 0xFFFFFFFF, f"{name}{tuple(hex(a) for a in args)}: {got:#x} != {value & 0xFFFFFFFF:#x}")
        require(self.w.log == log, f"{name}: log {self.w.log!r} != {log!r}")
        self.w.check_lists(name)


def model_value_at(image, index):
    """FUN_00625620: the value of plain entry `index`, 0 when out of range."""
    count = len(image.arr["plain"])
    if sw.s32(index) >= count or (mut("index-le") and index == count):
        return 0 if not mut("index-le") else 0xDEAD
    return image.arr["plain"][sw.s32(index)][1]


def check_scalars(rig, rng):
    """The one-field getters and setters."""
    w = rig.w
    simple = ((LIST_C, 0x0C), (LIST_14, 0x14), (LIST_18, 0x18), (GET_1C, 0x1C), (GET_20, 0x20))
    for _ in range(40):
        w.models.clear()
        list_address = rig.make_list(rng)
        image = w.models[list_address]
        unit = rig.make_unit(list_address)
        for address, offset in simple:
            rig.expect(f"get{offset:#x}", address, (list_address,), image.f[offset], [], 1)
        for address, offset in simple[:3]:
            rig.expect(f"get{offset:#x}-null", address, (0,), 0, [], 1)
        want = 6 if not mut("type-null-zero") else 0
        rig.expect("type", LIST_TYPE, (list_address,), image.f[0x08], [], 1)
        rig.expect("type-null", LIST_TYPE, (0,), want, [], 1)
        rig.expect("plain-count", PLAIN_COUNT, (list_address,), len(image.arr["plain"]), [], 1)
        rig.expect("plain-count-null", PLAIN_COUNT, (0,), 0, [], 1)
        rig.expect("exp-count", EXP_COUNT, (unit,), len(image.arr["exp"]), [], 1)
        rig.expect("exp-count-null", EXP_COUNT, (0,), 0, [], 1)
        rig.expect("exp-count-nolist", EXP_COUNT, (rig.make_unit(0),), 0, [], 1)
        size = len(image.arr["plain"])
        for index in sorted({0, 1, 3, max(size - 1, 0), size, size + 2}):
            rig.expect("value-at", VALUE_AT, (list_address, index), model_value_at(image, index), [], 2)
        rig.expect("value-at-null", VALUE_AT, (0, 1), 0, [], 2)
        for address, offset in ((SET_1C, 0x1C), (SET_20, 0x20)):
            value = rng.randrange(1 << 32)
            w.run(address, list_address, value, nstack=2)
            image.f[offset] = value
            rig.cases += 1
            w.check_lists(f"set{offset:#x}")
        w.run(SET_100, unit, nstack=1)
        image.f[0x10] |= 0x100
        w.check_lists("set-100")
        w.run(SET_100, 0, nstack=1)
        w.run(SET_100, rig.make_unit(0), nstack=1)
        rig.cases += 2
        want_bit = (image.f[0x10] >> 8) & 1
        if mut("flag-bit"):
            want_bit = (image.f[0x10] >> 9) & 1
        rig.expect("test-100", TEST_100, (unit,), want_bit, [], 1)
        rig.expect("test-100-null", TEST_100, (0,), 0, [], 1)
        rig.expect("test-100-nolist", TEST_100, (rig.make_unit(0),), 0, [], 1)
        image.f[0x10] &= ~0x100 & 0xFFFFFFFF
        w.m.w32(list_address + 0x10, image.f[0x10])
        rig.expect("test-100-clear", TEST_100, (unit,), 0, [], 1)


def check_expanded_fields(rig, rng):
    """FUN_00625bb0 and FUN_00625be0: the +0x58 pointer and the bitmap after it."""
    w = rig.w
    for _ in range(40):
        w.models.clear()
        expanded = rng.choice((0, EXPANDED))
        list_address = rig.make_list(rng, flags=expanded | rng.randrange(1 << 30))
        image = w.models[list_address]
        unit = rig.make_unit(list_address)
        states = rng.choice((0, 1, 31, 32, 33, 100, 256, 0x7FFFFFFF, -5 & 0xFFFFFFFF))
        w.m.w32(w.tables + 0xC4, states)
        plus = image.f[0x58]
        rig.expect("exp-ptr", EXP_PTR, (unit,), plus if expanded else 0, [], 1)
        n = sw.s32(sw.s32(states) + 31)
        words = (n + (31 if n < 0 else 0)) >> 5 if not mut("bitmap-round-down") else sw.s32(states) >> 5
        rig.expect("bitmap", BITMAP, (unit,), plus + 4 * words if expanded else 0, [], 1)
        for address in (EXP_PTR, BITMAP):
            rig.expect("exp-null", address, (0,), 0, [], 1)
            rig.expect("exp-nolist", address, (rig.make_unit(0),), 0, [], 1)


def check_copy(rig, rng):
    """FUN_00625c90: copy min(count, n) plain entries into the caller's buffer."""
    w = rig.w
    for _ in range(40):
        w.models.clear()
        list_address = rig.make_list(rng)
        image = w.models[list_address]
        count = len(image.arr["plain"])
        # A negative n with a null array (count 0, no capacity) walks from address 0, so it is left out there.
        for n in sorted({0, 1, max(count - 1, 0), count, count + 3} | ({-2 & 0xFFFFFFFF} if count else set())):
            out = w.m.alloc(0x100)
            w.m.mu.mem_write(out, b"\xaa" * 0x100)
            take = min(count, sw.s32(n))
            if mut("copy-ignores-n"):
                take = count
            got, _ = w.run(COPY_PLAIN, list_address, out, n, nstack=3)
            expected = b"".join(struct.pack("<II", *e) for e in image.arr["plain"][:max(take, 0)])
            expected += b"\xaa" * (0x100 - len(expected))
            rig.cases += 1
            require(got == take & 0xFFFFFFFF, f"copy returned {got:#x}, expected {take & 0xFFFFFFFF:#x}")
            require(w.m.read(out, 0x100) == expected, f"copy buffer differs for n={sw.s32(n)}")
        rig.expect("copy-null", COPY_PLAIN, (0, 0, 5), 0, [], 3)
        w.check_lists("copy")


def check_parent_owner(rig, rng):
    """FUN_00625820: the parent list's owner and the 'not switched off' flag through the out pointer."""
    w = rig.w
    for _ in range(80):
        w.models.clear()
        parent = rig.make_list(rng, flags=rng.choice((EXPANDED, 0)) | rng.randrange(1 << 30))
        parent_image = w.models[parent]
        child = rig.make_list(rng, flags=rng.randrange(1 << 32))
        child_image = w.models[child]
        link = rng.choice((parent, 0))
        child_image.f[0x34] = link
        w.m.w32(child + 0x34, link)
        w.models[child] = child_image
        unit = rig.make_unit(child)
        flag = w.m.alloc(8)
        for flag_arg in (flag, 0):
            w.m.w32(flag, 0xCAFE)
            value, _ = w.run(PARENT_OWNER, unit, flag_arg, nstack=2)
            rig.cases += 1
            live = link != 0 and parent_image.f[0x10] & EXPANDED
            expected = parent_image.f[0x44] if live else 0
            expected_flag = 0xCAFE
            if flag_arg:
                expected_flag = ((~(child_image.f[0x10] >> 30)) & 1) if live else 0
                if mut("owner-flag-inverted") and live:
                    expected_flag ^= 1
            require(value == expected, f"owner {value:#x} != {expected:#x}")
            require(w.m.r32(flag) == expected_flag, f"flag {w.m.r32(flag):#x} != {expected_flag:#x}")
        w.m.w32(flag, 0xCAFE)
        require(w.run(PARENT_OWNER, 0, flag, nstack=2)[0] == 0 and w.m.r32(flag) == 0, "null unit")
        require(w.run(PARENT_OWNER, 0, 0, nstack=2)[0] == 0, "null unit and flag")
        w.check_lists("parent-owner")


def check_free_and_drop(rig, rng):
    """FUN_00625960 frees the changed-stat queue; FUN_00626D10 drops a unit's list."""
    w = rig.w
    for _ in range(60):
        w.models.clear()
        expanded = rng.choice((0, EXPANDED))
        list_address = rig.make_list(rng, flags=expanded | rng.randrange(1 << 30))
        image = w.models[list_address]
        had_buffer = bool(image.cap["queue"] or image.arr["queue"])
        unit = rig.make_unit(list_address)
        log = []
        if expanded:
            if had_buffer:
                log = [("free", image.f[0x00], True, 0xA1E, ".\\Stats\\StatsEx.cpp")]
            image.arr["queue"], image.cap["queue"] = [], 0
        w.run(FREE_QUEUE, unit, nstack=1)
        rig.cases += 1
        require(w.log == log, f"free-queue log {w.log!r} != {log!r}")
        w.check_lists("free-queue")
        w.run(FREE_QUEUE, 0, nstack=1)
        w.run(FREE_QUEUE, rig.make_unit(0), nstack=1)
        require(w.log == [], "free-queue touched the allocator for null input")
        # FUN_00626d10
        list_address = rig.make_list(rng, flags=rng.choice((0, EXPANDED)) | rng.randrange(1 << 30))
        unit = rig.make_unit(list_address)
        w.run(DROP_LIST, unit, nstack=1)
        rig.cases += 1
        flags = w.models[list_address].f[0x10]
        require(w.log == ([("release", list_address)] if flags & EXPANDED else []), f"drop log {w.log!r}")
        require(w.m.r32(unit + 0x5C) == 0, "drop left the list pointer")
        w.run(DROP_LIST, rig.make_unit(0), nstack=1)
        require(w.log == [], "drop of an empty unit called release")
        w.check_lists("drop")


def check_wrappers(rig, rng):
    """The thin wrappers over the three stat getters and the percent helper."""
    w = rig.w
    for _ in range(80):
        w.models.clear()
        list_address = rig.make_list(rng)
        unit = rig.make_unit(list_address)
        stat, layer, other, flag = rng.randrange(200), rng.randrange(8), rng.randrange(200), rng.choice((0, 1, 5))
        g = lambda kind, *args: mix(kind, *args)   # noqa: E731  (the hooked getters' answers)
        rig.expect("plain-thunk", THUNK_PLAIN, (list_address, stat, layer), g(1, list_address, stat, layer),
                   [("get", 1, list_address, stat, layer)], 3)
        rig.expect("exp-thunk", THUNK_EXP, (list_address, stat, layer), g(2, list_address, stat, layer),
                   [("get", 2, list_address, stat, layer)], 3)
        base = g(2, list_address, stat, layer) - g(1, list_address, stat, layer)
        if mut("diff-reversed"):
            base = -base
        rig.expect("diff", DIFF, (list_address, stat, layer), base,
                   [("get", 2, list_address, stat, layer), ("get", 1, list_address, stat, layer)], 3)
        rig.expect("diff-null", DIFF, (0, stat, layer), 0, [], 3)
        rig.expect("plain-at", PLAIN_AT, (list_address, stat), g(2, list_address, stat, 0),
                   [("get", 2, list_address, stat, 0)], 2)
        rig.expect("unit-exp", UNIT_EXP, (rng.randrange(1 << 32), unit, stat), g(3, unit, stat, 0),
                   [("get", 3, unit, stat, 0)], 3)
        for address, first in ((BLEND, list_address), (UNIT_BLEND, unit)):
            args = (first, stat, other, flag)
            base_value = g(1, list_address, stat, 0)
            scale = g(2, list_address, other, 0)
            log = [("get", 2, list_address, other, 0), ("get", 1, list_address, stat, 0)]
            extra = 0
            if flag:
                log.append(("get", 2, list_address, stat, 0))
                extra = g(2, list_address, stat, 0) - base_value
            log.append(("percent", base_value, scale, 0x64))
            rig.expect("blend", address, args, mix(8, base_value, scale, 0x64) + extra + base_value, log, 4)
        rig.expect("blend-null", BLEND, (0, 1, 2, 3), 0, [], 4)


def check_named(rig, rng):
    """FUN_00625e60 and kin: the row for a fixed stat, looked up in the list's expanded array."""
    w = rig.w
    for address, (primary, secondary) in NAMED.items():
        for _ in range(20):
            w.models.clear()
            list_address = rig.make_list(rng)
            unit = rig.make_unit(list_address)
            two = address in (0x625E60, 0x625EF0)
            for which in ((0, 1) if two else (None,)):
                stat = primary if not which else secondary
                if mut("which-swap") and two:
                    stat = secondary if not which else primary
                args = (unit,) if which is None else (unit, which)
                log = [("row", list_address, stat << 16, w.row(stat))]
                rig.expect(f"named-{address:x}", address, args, mix(9, list_address, stat << 16, w.row(stat)),
                           log, len(args))
                # The table count at the stat id is the last count that has no row.
                w.m.w32(w.tables + 0xBD4, stat)
                rig.expect(f"named-{address:x}-short", address, args, 0, [], len(args))
                w.m.w32(w.tables + 0xBD4, stat + 1)
                rig.expect(f"named-{address:x}-edge", address, args, mix(9, list_address, stat << 16, w.row(stat)),
                           log, len(args))
                w.m.w32(w.tables + 0xBD4, sw.ROWS)
                rig.expect(f"named-{address:x}-nolist", address, (rig.make_unit(0),) + args[1:], 0, [], len(args))
                rig.expect(f"named-{address:x}-null", address, (0,) + args[1:], 0, [], len(args))


def run_all(seed, rounds):
    """Build the rig and run every group; returns (case count)."""
    rng = random.Random(seed)
    rig = Rig()
    for _ in range(rounds):
        check_scalars(rig, rng)
        check_expanded_fields(rig, rng)
        check_copy(rig, rng)
        check_parent_owner(rig, rng)
        check_free_and_drop(rig, rng)
        check_wrappers(rig, rng)
        check_named(rig, rng)
    return rig.cases


def main():
    """Run the oracle, or with --mutation-check require every model defect to be caught."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mutation-check", action="store_true", help="break the model on purpose and require failures")
    options = parser.parse_args()
    if options.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run_all(1, 1)
            except Mismatch:
                print(f"caught: {name}")
                continue
            sys.exit(f"mutation {name} was not caught")
        print(f"ok: {len(MUTATIONS)} model mutations caught")
        return
    cases = run_all(1, 3)
    print(f"ok: {cases} stat-list field accessor, wrapper and named-stat getter cases")


if __name__ == "__main__":
    main()
