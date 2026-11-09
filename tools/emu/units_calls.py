# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the UNITS\\ routines that allocate, free or call out (Units.cpp, Missile.cpp, UnitFinds.cpp, MisStream.cpp) in stock game.exe 1.14d.

    uv run python units_calls.py [--mutation-check]

Same rig as units_small.py (random fake units, a model written from the
decompile and disassembly, whole-block comparison, the ordered log of the
hooked callees, the bytes `ret n` popped, fatal lines). The pool entries
0x40_b430 / 0x40_b480 log (pool = ECX, size or block = EDX, file, line, flag);
0x40_b4b0 (grow) logs and returns a fresh block. The data-tables pointer
(0x744304) is a block with the MonStats array (+0xa78 base, +0xa80 count,
0x1a8 bytes a row) and the Skills array (+0xb98 base, +0xba0 count, 0x23c a row).

Covered: FUN_00620300 (unit free), 00620d80 (monster speed word), 00621c30 and
00621cc0 (the 0x161 / 0x162 stat pair, ESI / EBX convention), 00621ed0,
00621f90 (player data allocation), 00622060 (its release), 00622830, 00622de0,
00623600, 006238c0, 006219f0, 0064a100, 0064a150, 0065a900, 00663d40,
00663d90.

`--mutation-check` breaks the model on purpose, one defect at a time, and
requires each to fail.
"""
import random
import struct
import sys

import _monster_world as mw
import units_small as us
from _monster_world import require
from units_small import MASK, Scene, kinds, make_unit, null_halt, rand32, signed

MUTATIONS = ("free-monster-keeps-data", "free-id-zero", "free-type-keeps", "speed-unsigned-divide", "speed-mode-f-wrong-word",
             "speed-clamp-low", "speed-negative", "speed-no-stats-scaled", "cost-signed-compare", "cost-life-only",
             "cost-shift-3", "stat-pair-flag-always", "stat-pair-swapped", "stat-pair-new-list-kept",
             "item-130-wrong-type", "player-data-pairs-swapped", "player-data-4c", "player-free-order",
             "player-free-keeps-pointer", "weapon-null-found", "corpse-bit-swapped", "corpse-mode", "face-y-first",
             "face-tile-equal", "cost-arith-shift", "free-writes-after", "node-row-negative", "node-null-unit-first", "missile-id", "missile-free-keeps",
             "find-grow-14", "stream-free-skips-first", "stream-null-unit-pool")
mutation = None
COVERED = (0x62_0300, 0x62_0d80, 0x62_1c30, 0x62_1cc0, 0x62_1ed0, 0x62_1f90, 0x62_2060, 0x62_2830, 0x62_2de0, 0x62_3600, 0x62_38c0, 0x62_19f0, 0x64_a100, 0x64_a150, 0x65_a900, 0x66_3d40, 0x66_3d90)
MON = 0x744304
FILES = {"units": ".\\UNITS\\Units.cpp", "missile": ".\\UNITS\\Missile.cpp", "finds": ".\\UNITS\\UnitFinds.cpp",
         "stream": ".\\UNITS\\MisStream.cpp"}


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


class Rig(us.Rig):
    """units_small's rig with the stat, list, player-data and path callees these routines reach."""

    def __init__(self):
        """Hook the extra callees; script keys are named per hook."""
        super().__init__()
        lg = self.logger
        self.counter = 0
        self.watch, self.seen = None, []
        lg(0x62_5480, 3, "stat", ret=lambda c: self.script["stat"].get(c.arg(1), 0), regs=())
        lg(0x62_5D10, 1, "max-life", ret=lambda c: self.script["max_life"], regs=())
        lg(0x62_5D60, 1, "max-mana", ret=lambda c: self.script["max_mana"], regs=())
        lg(0x62_6D10, 1, "stats-free", regs=())
        lg(0x64_3A60, 1, "skills-free", regs=())
        lg(0x63_9DB0, 3, "state", regs=())
        lg(0x62_56B0, 2, "find-list", ret=lambda c: self.script["list"], regs=())
        lg(0x62_51F0, 5, "new-list", ret=lambda c: self.script["new"], regs=())
        lg(0x62_52D0, 2, "list-init", regs=())
        lg(0x62_6E10, 3, "socket-props", regs=())
        lg(0x62_7150, 4, "stat-set", regs=())
        lg(0x63_C9B0, 4, "inv-find", ret=self.inv_find, regs=())
        lg(0x45_1FE0, 0, "mon-row", ret=lambda c: self.script["mon_row"], regs=("ecx",))
        lg(0x63_A770, 1, "unit-63a770", ret=lambda c: self.script["ret"], regs=())
        lg(0x63_35F0, 1, "item-base", ret=lambda c: self.script["base"], regs=())
        lg(0x65_C430, 1, "pair-a", ret=lambda c: self.next_value(), regs=())
        lg(0x66_0F30, 1, "pair-b", ret=lambda c: self.next_value(), regs=())
        lg(0x4D_C0B0, 0, "unit-4dc0b0", ret=lambda c: self.next_value(), regs=("ecx",))
        lg(0x61_AE30, 1, "word-61ae30", ret=lambda c: self.next_value(), regs=())
        lg(0x66_A250, 1, "player-init", regs=())
        lg(0x66_A260, 0, "player-exit", regs=("ecx",))
        lg(0x65_C490, 2, "release-a", regs=())
        lg(0x66_0FD0, 2, "release-b", regs=())
        lg(0x64_8A00, 1, "target-x", ret=lambda c: self.script["tx"], regs=())
        lg(0x64_8A10, 1, "target-y", ret=lambda c: self.script["ty"], regs=())
        lg(0x64_8810, 1, "path-keep", regs=())
        lg(0x62_1DC0, 3, "face", regs=())
        self.m.hook(0x40_B4B0, self.grow, 4)

    def pool_free(self, current):
        """Pool release: note the watched unit's (type, id) as the release sees it, then log as usual."""
        if self.watch:
            self.seen.append((current.r32(self.watch), current.r32(self.watch + 0xC)))
        return super().pool_free(current)

    def next_value(self):
        """A fresh scripted value each call (so each result differs)."""
        self.counter += 1
        return 0x1000 + self.counter * 0x111

    def inv_find(self, current):
        """0x63_c9b0 (inventory, &a, &b, &c): store the scripted triple, return the scripted found flag."""
        for index, key in enumerate(("w_a", "w_b", "w_c")):
            current.w32(current.arg(1 + index), self.script.get(key, 0))
        return self.script["found"]

    def grow(self, current):
        """0x40_b4b0 (ECX pool, EDX old block; size, file, line, flag): log it, return a fresh block."""
        self.log.append(("grow", current.mu.reg_read(mw.REGISTERS["ecx"]), current.mu.reg_read(mw.REGISTERS["edx"]), current.arg(0),
                         current.cstr(current.arg(1)), current.arg(2), current.arg(3)))
        return current.alloc(min(current.arg(0), 0x1000))   # the size is only logged


def tables(rig, rng, mon_count, skill_count):
    """The data-tables block at 0x744304: MonStats (0xa78 / 0xa80) and Skills (0xb98 / 0xba0) arrays; returns the two bases."""
    block = rig.m.alloc(0x1000)
    mons, skills = rig.m.alloc(0x1a8 * max(mon_count, 1) + 64), rig.m.alloc(0x23c * max(skill_count, 1) + 64)
    rig.write(mons, rng.randbytes(0x1a8 * max(mon_count, 1)))
    rig.m.w32(block + 0xA78, mons)
    rig.m.w32(block + 0xA80, mon_count)
    rig.m.w32(block + 0xB98, skills)
    rig.m.w32(block + 0xBA0, skill_count)
    rig.m.w32(MON, block)
    return mons, skills


def free_log(pool, block, line, key="units", flag=0):
    """A pool release log entry."""
    return ("free", pool, block, FILES[key], line, flag)


def alloc_log(pool, size, line, key="units", flag=0):
    """A pool allocate log entry."""
    return ("alloc", pool, size, FILES[key], line, flag)


def check_unit_free(rig, rng):
    """FUN_00620300 (unit): nothing for null; else the stats free (0x62_6d10), the skills free (0x64_3a60), a monster's data (+0x14) released (line 0x69a, the unit still type and id intact), type = 6 and id (+0xc) = -1, then the unit released (line 0x6a4, so the pool release sees type 6 / id -1)."""
    for _ in range(40):
        kind = kinds(rng)
        sc = make_unit(rig, rng, kind)
        pool, data = sc.r(sc.unit + 8), sc.r(sc.unit + 0x14)
        ident = sc.r(sc.unit + 0xC)
        log = [("stats-free", sc.unit), ("skills-free", sc.unit)]
        seen = []       # (type, id) of the unit at each pool release
        if kind == 1 and not mut("free-monster-keeps-data"):
            log.append(free_log(pool, data, 0x69A))
            seen.append((kind, ident))
        sc.w(sc.unit, 6 if not mut("free-type-keeps") else kind)
        sc.w(sc.unit + 0xC, 0 if mut("free-id-zero") else MASK)
        log.append(free_log(pool, sc.unit, 0x6A4))
        seen.append((kind, ident) if mut("free-writes-after") else (6, MASK))
        rig.watch, rig.seen = sc.unit, []
        got = rig.go(0x62_0300, sc.unit)
        rig.watch = None
        require(got[0] == "ret" and rig.log == log, f"620300 type {kind}: {got} {rig.log} != {log}")
        require(rig.seen == seen, f"620300 type {kind}: (type, id) of the unit at each release {rig.seen} != {seen}")
        sc.check("620300")
    got = rig.go(0x62_0300, 0)
    require(got[0] == "ret" and rig.log == [], f"620300 null: {got} {rig.log}")


def check_speed(rig, rng):
    """FUN_00620d80 (unit): a monster's word +0x4c from its MonStats row (class +4; 0x1a8 a row): 0 for a bad class (signed range test, `jl` / `jge` in the disassembly: no table here is big enough to tell it from an unsigned one); +0x36 without stats; with stats stat 0x43 percent of +0x38 (mode 0xf) or +0x36 (signed words), truncated toward zero, 0 below 1, clamped to 0x7fff."""
    for _ in range(150):
        count = rng.choice((0, 1, 4, 9))
        mons, _ = tables(rig, rng, count, 0)
        cls = rng.choice((0, 1, count - 1, count, count + 1, MASK, 0x80000000, rng.randrange(count + 3)))
        kind = rng.choice((1, 1, 1, 0, 2))
        sc = make_unit(rig, rng, kind)
        sc.put(sc.unit + 4, cls)
        sc.put(sc.unit + 0x10, rng.choice((0xF, 0xF, 0, 1, 0xE)))
        if rng.random() < 0.3:
            sc.put(sc.unit + 0x5C, 0)
        pct = rng.choice((0, 1, 50, 100, 100, 150, 400, 1000, 1 << 20, MASK, MASK - 40, rng.randrange(300)))
        rig.script["stat"] = {0x43: pct}
        log = []
        if kind == 1:
            valid = 0 <= signed(cls) < count and mons + cls * 0x1A8 != 0
            if not valid:
                word = 0
            else:
                row = mons + cls * 0x1A8
                raw = struct.unpack("<H", rig.m.read(row + 0x36, 2))[0]
                if sc.r(sc.unit + 0x5C) == 0:
                    word = raw
                    if mut("speed-no-stats-scaled") and sc.r(sc.unit + 0x10) == 0xF:
                        word = struct.unpack("<H", rig.m.read(row + 0x38, 2))[0]
                else:
                    log = [("stat", sc.unit, 0x43, 0)]
                    pick = 0x38 if sc.r(sc.unit + 0x10) == 0xF and not mut("speed-mode-f-wrong-word") else 0x36
                    base = struct.unpack("<h", rig.m.read(row + pick, 2))[0]
                    product = signed(base * pct)
                    quotient = abs(product) // 100 * (1 if product >= 0 else -1)
                    if mut("speed-unsigned-divide"):
                        quotient = (product & MASK) // 100
                    if quotient <= 0 and not mut("speed-negative"):
                        quotient = 0
                    word = min(quotient, 0x7FFE if mut("speed-clamp-low") else 0x7FFF) & 0xFFFF
            sc.w(sc.unit + 0x4C, word, 2)
        got = rig.go(0x62_0D80, sc.unit)
        require(got[0] == "ret" and rig.log == log, f"620d80 type {kind}: {got} {rig.log} != {log}")
        sc.check(f"620d80 class {cls:#x}")
    got = rig.go(0x62_0D80, 0)
    require(got[0] == "ret", "620d80 null")


def check_vitals_cost(rig, rng):
    """FUN_00622de0 (unit): (missing life + missing mana, each (max - current) >> 8 when current < max, unsigned) * stat 0xc, the 32-bit product shifted right 2 (logical); 0 when nothing is missing."""
    for _ in range(120):
        sc = make_unit(rig, rng, 0)
        max_life, max_mana = rng.choice((0, 0x100, 0x2800, 0x7FFFFF, MASK, rng.randrange(1 << 32))), rng.choice((0, 0x100, 0x1900, rng.randrange(1 << 32)))
        cur_life, cur_mana = ((rng.choice((0, 0x80, max_life, max_life + 1, rng.randrange(1 << 32))) & MASK), rng.choice((0, max_mana, rng.randrange(1 << 32))))
        cost = rng.choice((0, 1, 4, 7, 100, MASK, 0x80000000, rng.randrange(1 << 32)))
        rig.script.update(max_life=max_life, max_mana=max_mana, stat={6: cur_life, 8: cur_mana, 0xC: cost})
        life = (max_life - cur_life) >> 8 if (cur_life < max_life if not mut("cost-signed-compare") else signed(cur_life) < signed(max_life)) else 0
        mana = 0 if mut("cost-life-only") else (max_mana - cur_mana) >> 8 if cur_mana < max_mana else 0
        log = [("max-life", sc.unit), ("max-mana", sc.unit), ("stat", sc.unit, 6, 0), ("stat", sc.unit, 8, 0)]
        if life == 0 and mana == 0:
            want = 0
        else:
            log.append(("stat", sc.unit, 0xC, 0))
            product = (cost * (life + mana)) & MASK
            want = (signed(product) >> 2) & MASK if mut("cost-arith-shift") else product >> (3 if mut("cost-shift-3") else 2)
        got = rig.go(0x62_2DE0, sc.unit)
        require(got == ("ret", want) and rig.log == log, f"622de0: {got} {rig.log} != {want:#x} {log}")
        sc.check("622de0")


def check_stat_pair(rig, rng):
    """FUN_00621c30 (ESI unit, EBX b; a on the stack) and FUN_00621cc0 (unit, a, b): +0x94 = a, +0x98 = b; with stats (+0x5c) find or make the state 0x62 list (type, id) and set stats 0x161 = a, 0x162 = b; flag 0x400 of +0xc8 set unless the list could not be made."""
    for wrapper in (False, True):
        for _ in range(60):
            sc = make_unit(rig, rng, kinds(rng))
            has_stats = rng.random() < 0.7
            if not has_stats:
                sc.put(sc.unit + 0x5C, 0)
            elif sc.r(sc.unit + 0x5C) == 0:
                sc.put(sc.unit + 0x5C, 0x1234)
            have_list, can_make = rng.random() < 0.4, rng.random() < 0.8
            rig.script.update(list=0x7000 if have_list else 0, new=0x7100 if can_make else 0)
            a, b = rand32(rng), rand32(rng)
            pool, kind, ident = sc.r(sc.unit + 8), sc.r(sc.unit), sc.r(sc.unit + 0xC)
            sc.w(sc.unit + 0x94, a)
            sc.w(sc.unit + 0x98, b)
            log = []
            flag = True
            if has_stats:
                log += [("state", sc.unit, 0x62, 1), ("find-list", sc.unit, 0x62)]
                handle = rig.script["list"]
                if not have_list:
                    log.append(("new-list", pool, 0, 0, kind, ident))
                    handle = rig.script["new"]
                    if handle == 0:
                        flag = False
                    else:
                        log += [] if mut("stat-pair-new-list-kept") else [("list-init", handle, 0x62)]
                        log.append(("socket-props", sc.unit, handle, 1))
                if flag:
                    pair = ((0x161, b, 0), (0x162, a, 0)) if mut("stat-pair-swapped") else ((0x161, a, 0), (0x162, b, 0))
                    log += [("stat-set", handle) + pair[0], ("stat-set", handle) + pair[1]]
            if flag or mut("stat-pair-flag-always"):
                sc.w(sc.unit + 0xC8, sc.r(sc.unit + 0xC8) | 0x400)
            if wrapper:
                got = rig.go(0x62_1CC0, sc.unit, a, b)
            else:
                got = rig.go(0x62_1C30, a, ecx=0, regs={"esi": sc.unit, "ebx": b}, nstack=1)
            require(got[0] == "ret" and rig.log == log, f"{'621cc0' if wrapper else '621c30'}: {got} {rig.log} != {log}")
            sc.check("stat pair")
    got = rig.go(0x62_1C30, 5, regs={"esi": 0, "ebx": 6}, nstack=1)
    require(got[0] == "ret" and rig.log == [], "621c30 null")


def check_item_130(rig, rng):
    """FUN_00621ed0 (item): byte +0x130 of the item base 0x63_35f0(class); 0xf20 for null or a non-item, 0xf24 when the base is missing."""
    for _ in range(60):
        kind = rng.choice((4, 4, 4, 0, 1, 2))
        sc = make_unit(rig, rng, kind)
        base_sc = Scene(rig, rng)
        base = base_sc.block(0x200)
        rig.script["base"] = base if rng.random() < 0.8 else 0
        if kind != 4:
            want, log = ("halt", 0xF20), []
        elif rig.script["base"] == 0:
            want, log = ("halt", 0xF24), [("item-base", sc.r(sc.unit + 4))]
        else:
            byte = base_sc.b(base + (0x131 if mut("item-130-wrong-type") else 0x130))
            want, log = ("ret", byte), [("item-base", sc.r(sc.unit + 4))]
        got = rig.go(0x62_1ED0, sc.unit)
        require(got == want and rig.log == log, f"621ed0 type {kind}: {got} {rig.log} != {want} {log}")
        sc.check("621ed0")
        base_sc.check("621ed0 base")
    null_halt(rig, 0x62_1ED0, 0xF20, 0)


def check_player_data(rig, rng):
    """FUN_00621f90 (unit): allocate 0x16c bytes (zeroed) of player data at +0x14, +0x10 + 4i = the pair-a function (0x65_c430) of the unit-4dc0b0 callee (0x4d_c0b0), +0x1c + 4i = the pair-b function (0x66_0f30) of the same, for i = 0..2, +0x2c = the word function (0x61_ae30) of 1, +0x4c = 0, then the player-init callee (0x66_a250) with the unit; 0xf4f null, 0xf51 data present. FUN_00622060 (pool, unit): releases those six (0x65_c490, 0x66_0fd0), the player-exit callee (0x66_a260), the data (line 0xf76), +0x14 = 0; 0xf6b null, 0xf6d no data."""
    for _ in range(30):
        sc = make_unit(rig, rng, kinds(rng))
        pool = sc.r(sc.unit + 8)
        sc.put(sc.unit + 0x14, 0)
        rig.counter = base = rng.randrange(1 << 20)
        got = rig.go(0x62_1F90, sc.unit)
        require(got[0] == "ret", f"621f90: {got}")
        data = rig.m.r32(sc.unit + 0x14)
        values = [0x1000 + (base + n) * 0x111 for n in range(1, 16)]
        expect = bytearray(0x16C)
        log = [alloc_log(pool, 0x16C, 0xF52)]
        n = 0
        for i in range(3):
            r1, a_arg, r2, b_arg = values[n + 1], values[n], values[n + 3], values[n + 2]
            n += 4
            log += [("unit-4dc0b0", sc.unit), ("pair-a", a_arg), ("unit-4dc0b0", sc.unit), ("pair-b", b_arg)]
            if mut("player-data-pairs-swapped"):
                r1, r2 = r2, r1
            struct.pack_into("<I", expect, 0x10 + 4 * i, r1)
            struct.pack_into("<I", expect, 0x1C + 4 * i, r2)
        word = values[n]
        log += [("word-61ae30", 1), ("player-init", sc.unit)]
        struct.pack_into("<I", expect, 0x2C, word)
        if mut("player-data-4c"):
            struct.pack_into("<I", expect, 0x4C, 1)
        require(rig.m.read(data, 0x16C) == bytes(expect), "621f90 player data bytes")
        require(rig.log == log, f"621f90 log {rig.log} != {log}")
        got = rig.go(0x62_1F90, sc.unit)
        require(got == ("halt", 0xF51), f"621f90 twice: {got}")
        # the release
        sc.put(sc.unit + 0x14, data)
        a_list = [("release-a", pool, struct.unpack_from("<I", expect, 0x10 + 4 * i)[0]) for i in range(3)]
        b_list = [("release-b", pool, struct.unpack_from("<I", expect, 0x1C + 4 * i)[0]) for i in range(3)]
        log = []
        for i in range(3):
            log += [a_list[i], b_list[i]] if not mut("player-free-order") else [b_list[i], a_list[i]]
        log += [("player-exit", sc.unit), free_log(pool, data, 0xF76)]
        got = rig.go(0x62_2060, pool, sc.unit)
        require(got[0] == "ret" and rig.log == log, f"622060: {got} {rig.log} != {log}")
        sc.w(sc.unit + 0x14, data if mut("player-free-keeps-pointer") else 0)
        sc.check("622060")
        got = rig.go(0x62_2060, pool, sc.unit)
        require(got == ("halt", 0xF6D), f"622060 without data: {got}")
    null_halt(rig, 0x62_1F90, 0xF4F, 0)
    null_halt(rig, 0x62_2060, 0xF6B, 0x1234, 0)


def check_weapon_and_corpse(rig, rng):
    """FUN_00622830 (monster): the first output of the inventory finder (0x63_c9b0, inventory +0x60) when it reports found, else 0 (0 for no inventory or a non-monster). FUN_00623600 (unit): a monster in mode 0xc whose MonStats2 row (GetMonStats2 0x45_1fe0, by class) has byte +4 & 0x80 set, byte +6 & 8 clear (entries [7] and [3] of the Fog bit-mask table 0x6ce268, read as bytes) and the unit-63a770 callee returning 0; 1 or 0."""
    for _ in range(80):
        kind = rng.choice((1, 1, 1, 0, 2))
        sc = make_unit(rig, rng, kind)
        if rng.random() < 0.2:
            sc.put(sc.unit + 0x60, 0)
        rig.script.update(found=rng.choice((0, 1, 5)), w_a=rng.randrange(1, 1 << 32), w_b=rng.randrange(1 << 32), w_c=rng.randrange(1 << 32))
        inventory = sc.r(sc.unit + 0x60)
        want = 0
        if kind == 1 and inventory:
            want = rig.script["w_a"] if rig.script["found"] or mut("weapon-null-found") else 0
        got = rig.go(0x62_2830, sc.unit)
        require(got == ("ret", want), f"622830 type {kind}: {got} != {want}")
        # the three out pointers are stack addresses, so only the inventory (first argument) is compared
        if kind == 1 and inventory:
            require(len(rig.log) == 1 and rig.log[0][:2] == ("inv-find", inventory), f"622830 log {rig.log}")
        else:
            require(rig.log == [], f"622830 log {rig.log}")
        sc.check("622830")
    for _ in range(120):
        kind = rng.choice((1, 1, 1, 0))
        sc = make_unit(rig, rng, kind)
        sc.put(sc.unit + 0x10, rng.choice((0xC, 0xC, 0xC, 0xD)))
        row_sc = Scene(rig, rng)
        row = row_sc.block(0x40)
        row_sc.put(row + 4, rng.choice((0x80, 0xFF, 0x7F, 0)), 1)
        row_sc.put(row + 6, rng.choice((0, 8, 0x77, 0xF7)), 1)
        rig.script.update(mon_row=row if rng.random() < 0.85 else 0, ret=rng.choice((0, 1, 9)))
        mode_ok = sc.r(sc.unit + 0x10) == (0xC if not mut("corpse-mode") else 0xD)
        log = []
        want = 0
        if kind == 1 and mode_ok:
            log = [("mon-row", sc.r(sc.unit + 4))]
            if rig.script["mon_row"]:
                flags4, flags6 = row_sc.b(row + 4), row_sc.b(row + 6)
                if mut("corpse-bit-swapped"):
                    flags4, flags6 = flags6, flags4
                if not (flags6 & 8) and (flags4 & 0x80):
                    log.append(("unit-63a770", sc.unit))
                    want = int(rig.script["ret"] == 0)
        got = rig.go(0x62_3600, sc.unit)
        require(got == ("ret", want) and rig.log == log, f"623600 type {kind}: {got} {rig.log} != {want} {log}")
        sc.check("623600")
        row_sc.check("623600 row")
    require(rig.go(0x62_3600, 0) == ("ret", 0), "623600 null")
    require(rig.go(0x62_2830, 0) == ("ret", 0), "622830 null")


def check_face(rig, rng):
    """FUN_006238c0 (unit): at the path's target (0x64_8a00 x / 0x64_8a10 y equal to the unit's x / y; static types read path +0xc / +0x10) keep the direction (0x64_8810, path); else face the target (0x62_1dc0, hooked: unit, target x, target y); 0xe83 null."""
    for _ in range(120):
        kind = kinds(rng)
        sc = make_unit(rig, rng, kind)
        static = us.is_static(kind)
        if not static and rng.random() < 0.15:
            sc.put(sc.unit + 0x2C, 0)
        path = sc.r(sc.unit + 0x2C)
        if static:
            ux, uy = sc.r(path + 0xC), sc.r(path + 0x10)
        else:
            ux, uy = (rand32(rng), rand32(rng)) if path else (0, 0)
        tx = ux if rng.random() < 0.5 else rand32(rng)
        ty = uy if rng.random() < 0.5 else rand32(rng)
        rig.script.update(tx=tx, ty=ty, by_path={path: ux} if not static and path else {},
                          by_path_y={path: uy} if not static and path else {})
        log = [("path-x", path)] if not static and path else []
        log.append(("target-x", path))
        if tx == ux:
            log += [("path-y", path)] if not static and path else []
            log.append(("target-y", path))
            if ty == uy and not mut("face-tile-equal"):
                log.append(("path-keep", path))
            else:
                log += [("target-y", path), ("target-x", path)]
                log.append(("face", sc.unit, tx, ty) if not mut("face-y-first") else ("face", sc.unit, ty, tx))
        else:
            log += [("target-y", path), ("target-x", path)]
            log.append(("face", sc.unit, tx, ty) if not mut("face-y-first") else ("face", sc.unit, ty, tx))
        got = rig.go(0x62_38C0, sc.unit)
        require(got[0] == "ret" and rig.log == log, f"6238c0 type {kind}: {got} {rig.log} != {log}")
        sc.check("6238c0")
    null_halt(rig, 0x62_38C0, 0xE83, 0)


def check_skill_node(rig, rng):
    """FUN_006219f0 (unit, Skills row): the unit's skill-list (+0xa8, nodes {record, next}) node whose record is the table row (0x23c a row); a bad row (below 0, at or past the count, null) is fatal 0xdfb, a null unit 0x638 (the row is tested first)."""
    for _ in range(100):
        count = rng.choice((0, 1, 5, 12))
        _, skills = tables(rig, rng, 1, count)
        sc = make_unit(rig, rng, 0)
        row = rng.choice((0, 1, count - 1, count, count + 1, MASK, 0x80000000, rng.randrange(count + 2)))
        nodes = [sc.block(8) for _ in range(rng.randrange(0, 5))]
        picks = [rng.randrange(max(count, 1)) for _ in nodes]
        for index, (node, pick) in enumerate(zip(nodes, picks, strict=True)):
            sc.put(node, skills + pick * 0x23C)
            sc.put(node + 4, nodes[index + 1] if index + 1 < len(nodes) else 0)
        header = sc.block(16)
        sc.put(header + 4, nodes[0] if nodes else 0)
        sc.put(sc.unit + 0xA8, header if rng.random() < 0.9 else 0)
        unit = sc.unit if rng.random() > 0.15 else 0
        bad = signed(row) < 0 or signed(row) >= count
        if bad and not mut("node-row-negative"):
            want = ("halt", 0xDFB)
        elif unit == 0:
            want = ("halt", 0x638)
        else:
            want = ("ret", 0)
            if sc.r(sc.unit + 0xA8):
                for node, pick in zip(nodes, picks, strict=True):
                    if pick == row:
                        want = ("ret", node)
                        break
        if mut("node-null-unit-first") and unit == 0:
            want = ("halt", 0x638)
        got = rig.go(0x62_19F0, unit, row)
        require(got == want and rig.log == [], f"6219f0 count {count} row {row:#x} unit {unit:#x}: {got} {rig.log} != {want}")
        sc.check("6219f0")


def check_missile_data(rig, rng):
    """FUN_0064a100 (unit): a missile (type 3) with no data (+0x14 = 0) gets a zeroed 0x34-byte block (pool at unit +8, Missile.cpp line 0x22) stored at +0x14, its +0x1c = -1; a missile that has data is left alone. FUN_0064a150: a missile with data releases it (line 0x3e) and clears +0x14. Other unit types and a null unit: nothing."""
    for _ in range(60):
        kind = rng.choice((3, 3, 3, 0, 1, 4))
        sc = make_unit(rig, rng, kind)
        if rng.random() < 0.5:
            sc.put(sc.unit + 0x14, 0)
        pool, data = sc.r(sc.unit + 8), sc.r(sc.unit + 0x14)
        before = rig.m.brk
        got = rig.go(0x64_A100, sc.unit)
        if kind == 3 and data == 0:
            new = rig.m.r32(sc.unit + 0x14)
            expect = bytearray(0x34)
            struct.pack_into("<I", expect, 0x1C, MASK if not mut("missile-id") else 0)
            require(got[0] == "ret" and new >= before and rig.m.read(new, 0x34) == bytes(expect), "64a100 allocation")
            require(rig.log == [alloc_log(pool, 0x34, 0x22, "missile")], f"64a100 log {rig.log}")
            data = new
            sc.put(sc.unit + 0x14, data)
        else:
            require(got[0] == "ret" and rig.log == [], f"64a100 type {kind}: {got} {rig.log}")
            sc.check("64a100 untouched")
        data = rig.m.r32(sc.unit + 0x14)
        got = rig.go(0x64_A150, sc.unit)
        if kind == 3 and data:
            require(rig.log == [free_log(pool, data, 0x3E, "missile")], f"64a150 log {rig.log}")
            sc.w(sc.unit + 0x14, data if mut("missile-free-keeps") else 0)
        else:
            require(rig.log == [], f"64a150 type {kind}: {rig.log}")
        sc.check("64a150")
    for address in (0x64_A100, 0x64_A150):
        got = rig.go(address, 0)
        require(got[0] == "ret" and rig.log == [], f"{address:08x} null")


def check_find_list(rig, rng):
    """FUN_0065a900 (ESI list: +0 pool, +4 block, +0x1c count): grow the block by 15 entries ((count + 15) * 4 bytes, UnitFinds.cpp 0xc5) and store both; null ESI is fatal 0xc0."""
    for _ in range(40):
        sc = Scene(rig, rng)
        lst = sc.block(0x40)
        count = rng.choice((0, 1, 15, 30, 0x7FFFFFF0, rng.randrange(1 << 16)))
        sc.put(lst + 0x1C, count)
        old = sc.r(lst + 4)
        pool = sc.r(lst)
        step = 14 if mut("find-grow-14") else 15
        new_count = (count + step) & MASK
        got = rig.go(0x65_A900, regs={"esi": lst}, nstack=0)
        require(got[0] == "ret", f"65a900: {got}")
        require(rig.log == [("grow", pool, old, (new_count * 4) & MASK, FILES["finds"], 0xC5, 0)], f"65a900 log {rig.log}")
        sc.w(lst + 0x1C, new_count)
        sc.w(lst + 4, rig.m.r32(lst + 4))
        sc.check("65a900")
    got = rig.go(0x65_A900, regs={"esi": 0}, nstack=0)
    require(got == ("halt", 0xC0), f"65a900 null: {got}")


def check_stream(rig, rng):
    """FUN_00663d40 (unit): with no stream (a missile's data +0x14, slot +0x30; every other unit has none), allocate 8 zeroed bytes (MisStream.cpp 0x2e; pool +8, 0 for a null unit) and store it in a missile's slot, so any other unit leaks the block. FUN_00663d90: with a stream, release its first word's block (0x3b, only when non-zero) and the stream (0x3c); the slot keeps its pointer. A non-missile has no stream."""
    for _ in range(60):
        kind = rng.choice((3, 3, 3, 0, 4))
        sc = make_unit(rig, rng, kind)
        data = sc.r(sc.unit + 0x14)
        sc.put(data + 0x30, 0 if rng.random() < 0.4 else sc.block(8))
        pool = sc.r(sc.unit + 8)
        stream = sc.r(data + 0x30) if kind == 3 else 0
        before = rig.m.brk
        got = rig.go(0x66_3D40, sc.unit)
        if stream == 0:
            require(got[0] == "ret" and rig.log == [alloc_log(pool, 8, 0x2E, "stream")], f"663d40 type {kind}: {rig.log}")
            if kind == 3:
                new = rig.m.r32(data + 0x30)
                require(new >= before and rig.m.read(new, 8) == bytes(8), "663d40 allocation")
                sc.w(data + 0x30, new)
        else:
            require(got[0] == "ret" and rig.log == [], f"663d40 type {kind}: {rig.log}")
        sc.check("663d40")
        stream = rig.m.r32(data + 0x30) if kind == 3 else 0
        if stream and rng.random() < 0.5:
            word = rng.choice((0, 0x4000, rng.randrange(1, 1 << 32)))
            rig.m.w32(stream, word)
            if stream in sc.blocks:
                sc.w(stream, word)
        got = rig.go(0x66_3D90, sc.unit)
        log = []
        if stream:
            if rig.m.r32(stream) != 0 and not mut("stream-free-skips-first"):
                log.append(free_log(pool, rig.m.r32(stream), 0x3B, "stream"))
            log.append(free_log(pool, stream, 0x3C, "stream"))
        require(got[0] == "ret" and rig.log == log, f"663d90 type {kind}: {rig.log} != {log}")
        sc.check("663d90 slot kept")
    for address in (0x66_3D40, 0x66_3D90):
        got = rig.go(address, 0)
        require(got[0] == "ret", f"{address:08x} null: {got}")
    rig.go(0x66_3D40, 0)
    require(rig.log == [alloc_log(0 if not mut("stream-null-unit-pool") else 1, 8, 0x2E, "stream")], f"663d40 null unit: {rig.log}")


def run_all():
    """Run every check; returns the case count."""
    rng = random.Random(0x620D80)
    rig = Rig()
    tables(rig, rng, 4, 4)
    plan = [(check_unit_free, 3), (check_speed, 3), (check_vitals_cost, 3), (check_stat_pair, 3), (check_item_130, 3),
            (check_player_data, 3), (check_weapon_and_corpse, 3), (check_face, 3), (check_skill_node, 3),
            (check_missile_data, 3), (check_find_list, 3), (check_stream, 3)]
    cases = 0
    for check, rounds in plan:
        for _ in range(rounds):
            check(rig, rng)
            cases += 1
    missing = [f"{a:#x}" for a in COVERED if a not in us.CALLED]
    require(not missing, f"functions never called: {missing}")
    return cases


def main():
    """Command line entry."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "UNITS call-out check rounds")


if __name__ == "__main__":
    main()
