# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify game.exe 1.14d's MISSILES/MissMode.cpp (FUN_005ab630 .. FUN_005adcc0).

    D2_MPQ_DIR=<mpq folder> uv run python missmode.py

The 44 functions of the module: the server hit functions (Missiles.txt
SrvHitFunc, table at 0x73c840), the damage functions (SrvDmgFunc, called as
(game, missile, target, damage)), the server do-function dispatcher
FUN_005adbb0 and its event wrapper FUN_005adcc0, the expire/free wrapper
FUN_005ad9d0, FUN_005ada80 (pierce counter) and the burst helpers.

Each function runs for real on a synthetic Missiles.txt / Skills.txt table
(game.exe's sgptDataTable at 0x744304: missiles base +0xb64, count +0xb6c,
row 0x1a4; skills base +0xb98, count +0xba0, row 0x23c). Every callee outside
the module (unit accessors, stat reads and writes, CreatingMissile, skill
calcs, rooms, spawns) is hooked: it logs its registers, stack arguments and,
for CreatingMissile, the whole 0x5c-byte D2SkillArgStrc, and returns a scripted
value. A Python model written from the disassembly predicts the return value,
the call log and the memory the function writes; a difference stops the run.
The module's own callees (FUN_005ab700, FUN_005adbb0) and the Skills.txt line
getter FUN_0045c4b0 run for real. Nothing here plays a sound or reads an MPQ.
"""
import argparse
import hashlib
import itertools
import random
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
TABLE_PTR = 0x744304
MASKS = 0x6CE268            # Fog gadwBitMasks: bit n is the dword at MASKS + 4 * n
HIT_TABLE, HIT_SIZE = 0x73C840, 0x73C83C
DO_TABLE, DO_SIZE = 0x73C768, 0x73C71C
SPREAD = {"8dir_x": 0x6E2A58, "8dir_y": 0x6E2A38, "spread_x": 0x6E2938, "spread_y": 0x6E2838,
          "dir_x": 0x6E2618, "dir_y": 0x6E25F8}
NROWS, NSKILLS = 40, 24
LIMIT = 400000
ANY = object()
STAR_FP = 0x5AC040

# D2SkillArgStrc (0x5c bytes) offsets as MissMode.cpp fills them
F_FLAGS, F_OWNER, F_MISSILE, F_TXT, F_X, F_Y, F_UX, F_UY, F_VEL = 0, 4, 8, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x28
F_SKILL, F_LEVEL, F_LOOP, F_FRAMES, F_FP = 0x2C, 0x30, 0x34, 0x4C, 0x54

# Callee address -> (name, stack words). These are boundaries the module calls, hooked and logged, not
# functions this oracle verifies: the addresses are written with an underscore so tools/gaps.py does not count them.
CALLEES = {
    0x55_2FD0: ("owner", 0), 0x55_2F60: ("find", 1), 0x64_A280: ("skill", 1), 0x64_A210: ("level", 1),
    0x64_A730: ("tx", 1), 0x64_A710: ("set_tx", 2), 0x64_A760: ("set_ty", 2), 0x64_A3B0: ("remaining", 1),
    0x64_A300: ("total", 1), 0x59_FA30: ("create", 0), 0x64_88C0: ("path_x", 1), 0x64_8900: ("path_y", 1),
    0x45_ADF0: ("unit_x", 0), 0x45_AE20: ("unit_y", 0), 0x62_5480: ("stat", 3), 0x62_7260: ("set_stat", 4),
    0x62_5D10: ("max_hp", 1), 0x48_3360: ("percent", 1), 0x46_38A0: ("monflag", 0), 0x4E_6CA0: ("range", 0),
    0x4F_4110: ("param5", 0), 0x4E_6C70: ("param4", 0), 0x62_0BB0: ("room", 1), 0x61_AED0: ("portal_flag", 2),
    0x61_AB00: ("in_town", 1), 0x5A_9930: ("portal", 1), 0x64_6CA0: ("skill_calc", 4),
    0x64_7BC0: ("min_dmg", 4), 0x64_7D00: ("max_dmg", 4), 0x5D_DD00: ("terror", 4), 0x64_92A0: ("dist2", 4),
    0x5B_3570: ("rand_monster", 2), 0x56_D4E0: ("star", 1), 0x5D_5BF0: ("scatter", 6), 0x5A_9370: ("radial", 7),
    0x56_D810: ("dispatch", 3), 0x62_0C10: ("set_target", 2), 0x5C_7DB0: ("aura", 4), 0x64_4F20: ("elem_len", 4),
    0x64_39F0: ("skill_by_enum", 0), 0x64_44A0: ("param1", 1), 0x54_E600: ("spawn", 4), 0x5A_8C70: ("elem_dmg", 2),
    0x5A_91C0: ("clear_dmg", 1), 0x5A_9270: ("add_elem", 3), 0x64_B7C0: ("eval_calc", 5), 0x62_1E40: ("overlay", 3),
    0x62_4690: ("update_flag", 2), 0x64_0E90: ("obj_line", 1), 0x54_17D0: ("set_event", 4), 0x5D_0C40: ("blizzard", 4),
    0x54_44B0: ("quest", 2), 0x58_E920: ("tyrael", 1), 0x45_C390: ("seed_mod", 0), 0x45_C3E0: ("seed_range", 0),
    0x46_3740: ("room_near", 1), 0x55_5600: ("free", 0), 0x5A_D2F0: ("undead_bonus", 2),
}


def require(condition, message):
    """Stop the run when stock behaviour differs from the model."""
    if not condition:
        raise RuntimeError(message)


def s32(value):
    """A dword as a signed int."""
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value >> 31 else value


def tdiv(value, divisor):
    """C integer division (truncates toward zero)."""
    quotient = abs(value) // abs(divisor)
    return quotient if (value < 0) == (divisor < 0) else -quotient


def percent(value, pct):
    """The hooked D2ApplyPercent (FUN_00483360): value * pct / 100, truncated."""
    return tdiv(value * pct, 100)


class World:
    """The emulated game with a synthetic data table and logging callees."""

    def __init__(self, exe):
        """Map game.exe, build the tables and hook the module's callees."""
        self.m = machine = emu.Emu(str(exe))
        self.log = []
        self.rng = random.Random(1)
        self.mask = [machine.r32(MASKS + 4 * i) for i in range(8)]
        table = machine.alloc(0x1000)
        machine.w32(TABLE_PTR, table)
        self.mrows = machine.alloc(NROWS * 0x1A4)
        self.srows = machine.alloc(NSKILLS * 0x23C)
        for off, value in ((0xB64, self.mrows), (0xB6C, NROWS), (0xB98, self.srows), (0xBA0, NSKILLS)):
            machine.w32(table + off, value)
        self.game = machine.alloc(0x200)
        self.units, self.paths = {}, {}
        self.val = {"owner": {}, "skill": {}, "level": {}, "tx": {}, "remaining": {}, "total": {}, "room": {},
                    "pos": {}, "find": {}, "monflag": {}, "stat": {}, "oroom": {}}
        self.script = {}
        self.new_missiles = []
        for addr, (name, words) in CALLEES.items():
            machine.hook(addr, self._make_hook(addr, name, words), words)
        self.hit_stub = self._stub("hit", 1, lambda e: self._record("hit", e, 1) or self.script.get("hit", 0))
        self.do_stub = self._stub("do", 0, lambda e: self._record("do", e, 0) or self.script.get("do", 0))

    def _stub(self, name, words, fn):
        """A callable address the function tables can point at."""
        addr = emu.STUB + 16 * (len(self.m.slots) + 1)
        self.m.slots[addr] = (name, lambda e: (fn(e), words))
        return addr

    def norm(self, value):
        """Stack addresses are not comparable between runs."""
        return "L" if emu.STACK <= value < emu.STACK + emu.STACK_SIZE else value

    def _record(self, name, machine, words, extra=()):
        """Append (name, ecx, edx, stack args...) to the call log."""
        regs = (machine.mu.reg_read(UC_X86_REG_ECX), machine.mu.reg_read(UC_X86_REG_EDX))
        self.log.append((name,) + regs + tuple(self.norm(machine.arg(i)) for i in range(words)) + tuple(extra))

    def _make_hook(self, addr, name, words):
        """Build the logging hook for one callee."""
        def hook(machine):
            ecx = machine.mu.reg_read(UC_X86_REG_ECX)
            edx = machine.mu.reg_read(UC_X86_REG_EDX)
            arg0 = machine.arg(0) if words else 0
            extra = ()
            if name == "create":
                extra = (tuple(machine.r32(edx + 4 * i) for i in range(23)),)
            self._record(name, machine, words, extra)
            return self._value(name, ecx, edx, arg0, machine)
        return hook

    def _value(self, name, ecx, edx, arg0, machine):
        """Scripted result of a hooked callee."""
        v = self.val
        if name == "owner":
            return v["owner"].get(edx, 0)
        if name in ("skill", "level", "tx", "remaining", "total"):
            return v[name].get(arg0, 0)
        if name == "set_tx":
            v["tx"][arg0] = machine.arg(1)
            return 0
        if name in ("unit_x", "unit_y"):
            return v["pos"].get(ecx, (0, 0))[name == "unit_y"]
        if name in ("path_x", "path_y"):
            return self.paths.get(arg0, (0, 0))[name == "path_y"]
        if name == "create":
            return self.new_missiles.pop(0) if self.new_missiles else 0
        if name == "percent":
            return percent(ecx, edx)
        if name == "room":
            return v["room"].get(arg0, 0)
        if name == "find":
            return v["find"].get((edx, arg0), 0)
        if name == "monflag":
            return v["monflag"].get((ecx, edx), 0)
        if name == "stat":
            return v["stat"].get((arg0, machine.arg(1)), 0)
        if name == "in_town":
            return v["oroom"].get(arg0, 0)
        return self.script.get(name, 0) if not callable(self.script.get(name)) else self.script[name](ecx, edx, machine)

    # ---- state builders -------------------------------------------------
    def mrow(self, index, **fields):
        """Write Missiles.txt fields (offset names) into row `index`."""
        base = self.mrows + index * 0x1A4
        layout = {"f4": (4, 1), "f5": (5, 1), "dofunc": (0xC, 2), "hitfunc": (0xE, 2), "hs1": (0x24, 2),
                  "hs2": (0x26, 2), "hs3": (0x28, 2), "overlay": (0x36, 2), "p1": (0x38, 4), "p4": (0x44, 4),
                  "hp1": (0x4C, 4), "hp2": (0x50, 4), "dp1": (0x78, 4), "dp2": (0x7C, 4), "calc": (0x90, 4),
                  "b_ae": (0xAE, 1), "frame": (0xDC, 4), "collide": (0x186, 1)}
        for name, value in fields.items():
            off, size = layout[name]
            self.m.mu.mem_write(base + off, (value & ((1 << 8 * size) - 1)).to_bytes(size, "little"))
        return base

    def srow(self, index, calc0=0, calc3=0, param1=0):
        """Write the Skills.txt fields the module reads."""
        base = self.srows + index * 0x23C
        for off, value in ((0x138, calc0), (0x144, calc3), (0x14C, param1)):
            self.m.w32(base + off, value)
        return base

    def unit(self, kind=3, cls=0, pos=(0, 0), seed=(0, 0), flags=0, mode=0, path=True, **vals):
        """A unit record. vals: owner, skill, level, tx, remaining, total, room."""
        machine = self.m
        addr = machine.alloc(0x100)
        machine.w32(addr, kind)
        machine.w32(addr + 4, cls & 0xFFFFFFFF)
        machine.w32(addr + 0x10, mode)
        machine.w32(addr + 0x20, seed[0])
        machine.w32(addr + 0x24, seed[1])
        machine.w32(addr + 0xC4, flags)
        pth = machine.alloc(0x40) if path else 0
        machine.w32(addr + 0x2C, pth)
        if path:
            machine.w32(pth + 0xC, pos[0] & 0xFFFFFFFF)
            machine.w32(pth + 0x10, pos[1] & 0xFFFFFFFF)
            self.paths[pth] = pos
        self.val["pos"][addr] = pos
        self.units[addr] = pth
        for key, value in vals.items():
            self.val[key][addr] = value
        return addr

    def damage(self, **fields):
        """A D2DamageStrc (0x70 bytes) with named dwords."""
        addr = self.m.alloc(0x70)
        for off, value in fields.items():
            self.m.w32(addr + int(off[1:], 16), value & 0xFFFFFFFF)
        return addr

    # ---- running ----------------------------------------------------------
    def run(self, addr, ecx=0, edx=0, args=(), regs=None):
        """Call a module function; return (eax, call log)."""
        self.log = []
        eax = self.m.call(addr, *args, ecx=ecx, edx=edx, limit=LIMIT, regs=regs)
        require(self.m.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{addr:x}: did not return")
        return eax, self.log

    def hit(self, addr, unit, target=0):
        """Run a server hit function (game, missile, target)."""
        return self.run(addr, ecx=self.game, edx=unit, args=(target,))

    def dmg(self, addr, unit, target, damage):
        """Run a server damage function (game, missile, target, damage)."""
        return self.run(addr, ecx=self.game, edx=unit, args=(target, damage))

    def spread(self, key, count):
        """A static offset table of dwords."""
        return [s32(self.m.r32(SPREAD[key] + 4 * i)) for i in range(count)]

    def dwords(self, addr, count):
        """Signed dwords at addr."""
        return [s32(self.m.r32(addr + 4 * i)) for i in range(count)]


def arg(**fields):
    """An expected D2SkillArgStrc: zero but for the named offsets (F_*)."""
    words = [0] * 23
    for off, value in fields.items():
        words[int(off[1:]) // 4] = value & 0xFFFFFFFF
    return tuple(words)


def A(*pairs):
    """arg() from (offset, value) pairs."""
    return arg(**{f"o{off}": value for off, value in pairs})


def equal(got, want):
    """One log field: ANY matches, integers compare as 32-bit values, tuples elementwise."""
    if want is ANY:
        return True
    if isinstance(got, int) and isinstance(want, int):
        return (got - want) & 0xFFFFFFFF == 0
    if isinstance(got, tuple) and isinstance(want, tuple):
        return len(got) == len(want) and all(equal(a, b) for a, b in zip(got, want))
    return got == want


def same(got, want):
    """Log equality with ANY wildcards."""
    return len(got) == len(want) and all(equal(g, w) for g, w in zip(got, want))


def first_difference(got, want):
    """Index and entries of the first log mismatch (or the length difference)."""
    for index, (g, w) in enumerate(zip(got, want)):
        if not equal(g, w):
            return f"entry {index}: got {g}, want {w}"
    return f"length {len(got)} vs {len(want)}"


def check(label, result, want_ret, want_log):
    """Compare a (eax, log) pair with the model."""
    eax, log = result
    if eax & 0xFFFFFFFF != want_ret & 0xFFFFFFFF or not same(log, want_log):
        raise RuntimeError(f"{label}: eax {eax:#x} want {want_ret & 0xFFFFFFFF:#x}; {first_difference(log, want_log)}")


def EC(name, ecx, edx, *rest):
    """An expected log entry with both registers given first (for callers that unpack the stack words)."""
    return E(name, *rest, ecx=ecx, edx=edx)


def E(name, *rest, ecx=ANY, edx=ANY):
    """An expected log entry: E('skill', unit) / E('create', ecx=game, edx=ANY, ...)."""
    return (name, ecx, edx) + tuple(rest)


def build_cases(world):
    """Yield (label, thunk) for every function; each thunk runs and checks."""
    w, m = world, world.m
    game = w.game
    rng = w.rng
    mk = w.mrow
    mask = w.mask
    cases = []

    def case(label):
        def deco(fn):
            cases.append((label, fn))
            return fn
        return deco

    def pos_of(target):
        """The (x, y) 5ab630/5ab700 read from a target; the log of the reads."""
        kind = m.r32(target)
        pth = w.units[target]
        if kind in (2, 4, 5):
            return (w.val["pos"][target], [])
        if pth == 0:
            return ((0, 0), [])
        return (w.paths[pth], [E("path_x", pth), E("path_y", pth)])

    # -- guards shared by the Missiles.txt-row functions -------------------------------
    def bad_units():
        """Units whose row lookup fails: class -1 and class == row count."""
        return [w.unit(cls=-1), w.unit(cls=NROWS), w.unit(cls=NROWS + 5)]

    # -------- 005ab700 / 005ab820 : directional burst ---------------------------------
    def burst(owner, target, mid, skill, lvl, stride):
        """Expected log of FUN_005ab700 for a valid missile id."""
        (x, y), reads = pos_of(target)
        row_vel = s32(m.r32(w.mrows + mid * 0x1A4 + 0x38)) << 7
        tx, ty = w.spread("dir_x", 8), w.spread("dir_y", 8)
        out = list(reads)
        step = stride
        i = 0
        while i * 4 < 0x20:
            out.append(EC("create", game, ANY, *arg_words(
                [(F_FLAGS, 0x1F), (F_OWNER, owner), (F_MISSILE, target), (F_TXT, mid), (F_X, x), (F_Y, y),
                 (F_SKILL, skill), (F_LEVEL, lvl), (F_LOOP, lvl - 1), (F_VEL, row_vel),
                 (F_UX, tx[i]), (F_UY, ty[i])])))
            i += step
        return out

    def arg_words(pairs):
        """One-element tuple holding an expected skill-arg snapshot."""
        return (A(*pairs),)

    @case("5ab700")
    def _():
        """Check FUN_005ab700 against its model."""
        for kind, path in ((1, True), (2, True), (4, True), (5, True), (3, False), (0, True)):
            tgt = w.unit(kind=kind, cls=1, pos=(rng.randrange(1, 9999), rng.randrange(1, 9999)), path=path)
            own = w.unit(kind=1)
            mk(7, p1=rng.randrange(0, 40))
            for stride in (1, 2, 3, 8, 9):
                got = w.run(0x5AB700, ecx=game, edx=own, args=(tgt, 7, 0x55, 9, stride))
                check(f"5ab700 type {kind} stride {stride}", got, 1, burst(own, tgt, 7, 0x55, 9, stride))
        for bad in (-1, NROWS, NROWS + 3):
            got = w.run(0x5AB700, ecx=game, edx=w.unit(), args=(w.unit(), bad, 1, 1, 1))
            check(f"5ab700 id {bad}", got, 0, [])

    @case("5ab820")
    def _():
        """Check FUN_005ab820 against its model."""
        mk(3, hs1=5, hp1=0)
        mk(4, hs1=6, hp1=3)
        mk(5, hs1=-1, hp1=2)
        mk(6, hs1=5, hp1=2)
        mk(5 + 0, hs1=-1)
        mk(9, hs1=5)
        for cls, par, tgtgiven in ((3, 1, True), (4, 3, True), (4, 3, False), (6, 2, True)):
            owner = w.unit(kind=1)
            unit = w.unit(cls=cls, owner=owner, skill=0x41, level=7, pos=(10, 20))
            tgt = w.unit(kind=1, cls=1, pos=(33, 44)) if tgtgiven else 0
            real_tgt = tgt or unit
            got = w.hit(0x5AB820, unit, tgt)
            want = [E("owner", ecx=game, edx=unit), E("level", unit), E("skill", unit)]
            want += burst(owner, real_tgt, m.r16(w.mrows + cls * 0x1A4 + 0x24), 0x41, 7, par)
            check(f"5ab820 class {cls}", got, 3, want)
        check("5ab820 no owner", w.hit(0x5AB820, w.unit(cls=3), 0), 1, [E("owner", ecx=game, edx=ANY)])
        check("5ab820 hs1<0", w.hit(0x5AB820, w.unit(cls=5), 0), 1, [])
        check("5ab820 null", w.hit(0x5AB820, 0, 0), 1, [])
        for unit in bad_units():
            check("5ab820 bad class", w.hit(0x5AB820, unit, 0), 1, [])

    # -------- 005ab630 : sub-missile at a target (callback context) ---------------------
    @case("5ab630")
    def _():
        """Check FUN_005ab630 against its model."""
        owner, missile = w.unit(kind=1), w.unit()
        for maxc, hitc in ((0, 5), (-2, 9), (3, 2), (3, 3), (3, 4), (1, 0)):
            ctx = m.alloc(0x20)
            cbarg = m.alloc(0x20)
            for off, value in ((0, owner), (4, missile), (8, 0x66), (0xC, 4), (0x10, maxc), (0x14, 0x31)):
                m.w32(cbarg + off, value & 0xFFFFFFFF)
            m.w32(ctx, game)
            m.w32(ctx + 8, hitc)
            m.w32(ctx + 0xC, cbarg)
            for kind, path in ((1, True), (2, True), (5, True), (3, False)):
                tgt = w.unit(kind=kind, pos=(rng.randrange(1, 500), rng.randrange(1, 500)), path=path)
                got = w.run(0x5AB630, ecx=ctx, edx=tgt)
                if maxc > 0 and hitc >= maxc:
                    check(f"5ab630 cap {maxc}/{hitc}", got, 0, [])
                    continue
                (x, y), reads = pos_of(tgt)
                want = reads + [EC("create", game, ANY, *arg_words(
                    [(F_FLAGS, 0x20), (F_OWNER, owner), (F_MISSILE, missile), (F_SKILL, 0x66), (F_LEVEL, 4),
                     (F_TXT, 0x31), (F_UX, x), (F_UY, y)]))]
                check(f"5ab630 {maxc}/{hitc} type {kind}", got, 1, want)

    # -------- 005ab8d0 : stationary sub-missile ------------------------------------------
    @case("5ab8d0")
    def _():
        """Check FUN_005ab8d0 against its model."""
        mk(2, hs1=9, hp1=11)
        mk(3, hs1=9, hp1=0)
        mk(4, hs1=-1)
        w.srow(6, calc0=0x1234)
        w.srow(7, calc0=0x4321)
        for cls, calc_result, skill in ((2, 0, 6), (3, 20, 6), (3, 2, 7), (3, 5, 7), (3, 4, 6)):
            w.script["skill_calc"] = calc_result
            owner = w.unit(kind=0)
            unit = w.unit(cls=cls, owner=owner, skill=skill, level=3, pos=(70, 80))
            got = w.hit(0x5AB8D0, unit)
            want = [E("owner", ecx=game, edx=unit), E("skill", unit), E("level", unit)]
            frames = 11
            if cls == 3:
                want.append(E("skill_calc", owner, 0x1234 if skill == 6 else 0x4321, skill, 3))
                frames = max(calc_result, 5)
            want += [E("unit_x", ecx=unit), E("unit_y", ecx=unit), EC("create", game, ANY, *arg_words(
                [(F_FLAGS, 0x8001), (F_OWNER, owner), (F_TXT, 9), (F_X, 70), (F_Y, 80), (F_SKILL, skill),
                 (F_LEVEL, 3), (F_FRAMES, frames)]))]
            check(f"5ab8d0 class {cls} calc {calc_result}", got, 1, want)
        for skill in (-1, NSKILLS, NSKILLS + 3):
            owner = w.unit()
            unit = w.unit(cls=2, owner=owner, skill=skill, level=1)
            check(f"5ab8d0 skill {skill}", w.hit(0x5AB8D0, unit), 1,
                  [E("owner", ecx=game, edx=unit), E("skill", unit), E("level", unit)])
        check("5ab8d0 no owner", w.hit(0x5AB8D0, w.unit(cls=2)), 1, [E("owner", ecx=game, edx=ANY)])
        check("5ab8d0 hs1<0", w.hit(0x5AB8D0, w.unit(cls=4)), 1, [])
        check("5ab8d0 null", w.hit(0x5AB8D0, 0), 1, [])
        for unit in bad_units():
            check("5ab8d0 bad class", w.hit(0x5AB8D0, unit), 1, [])
        w.script.pop("skill_calc", None)

    # -------- 005aba10 : terror on a monster near the minion ---------------------------
    @case("5aba10")
    def _():
        """Check FUN_005aba10 against its model."""
        w.srow(5)
        for rng_v, near, tgt_kind, minion_ok in ((10, True, 1, True), (10, False, 1, True), (10, True, 0, True),
                                                   (10, True, 1, False), (0, True, 1, True)):
            owner = w.unit(kind=0)
            unit = w.unit(owner=owner, skill=5, level=2, tx=0x777)
            minion = w.unit(kind=1, pos=(100, 100)) if minion_ok else 0
            w.val["find"][(3, 0x777)] = minion
            tgt = w.unit(kind=tgt_kind, cls=3, pos=(100 + (5 if near else 40), 100))
            w.script["range"] = rng_v
            w.script["dist2"] = lambda ecx, edx, mach: (s32(mach.arg(0)) - s32(mach.arg(2))) ** 2 + (s32(mach.arg(1)) - s32(mach.arg(3))) ** 2
            w.script["param5"] = 0x51
            w.script["param4"] = 0x41
            got = w.run(0x5ABA10, ecx=game, edx=unit, args=(tgt,))
            want = [E("skill", unit), E("level", unit), E("owner", ecx=game, edx=unit)]
            if tgt_kind == 1:
                want += [E("tx", unit), EC("find", game, 3, *(0x777,))]
                if minion_ok:
                    want += [E("range", ecx=5, edx=2), E("unit_y", ecx=tgt), E("unit_x", ecx=tgt),
                             E("unit_y", ecx=minion), E("unit_x", ecx=minion),
                             E("dist2", 100, 100, 100 + (5 if near else 40), 100)]
                    dx = 5 if near else 40
                    if dx * dx < rng_v * rng_v:
                        want += [E("param5", ecx=5), E("param4", ecx=5),
                                 E("terror", tgt, 5, 0x41, 0x51, ecx=game, edx=minion)]
            check(f"5aba10 {rng_v} {near} {tgt_kind} {minion_ok}", got, 1, want)
        for key in ("range", "param5", "param4", "dist2"):
            w.script.pop(key, None)

    # -------- 005abc40 / 005ac940 : fan of sub-missiles ----------------------------------
    def fan(label, addr, base_flag_hs, xkey, ykey, count, bound):
        """Run a table-walking hit function with several steps and new-missile results."""
        mk(8, hs1=14, hp1=1)
        mk(9, hs1=14, hp1=0)
        mk(10, hs1=14, hp1=3)
        mk(11, hs1=-1)
        xs, ys = w.spread(xkey, count), w.spread(ykey, count)
        for cls in (8, 9, 10):
            par = max(m.r32(w.mrows + cls * 0x1A4 + 0x4C), 1)
            for made in (False, True):
                owner = w.unit(kind=1)
                unit = w.unit(cls=cls, owner=owner, skill=0x20, level=6)
                steps = len(range(0, bound // 4, par))
                w.new_missiles[:] = [w.unit() for _ in range(steps)] if made else []
                news = list(w.new_missiles)
                got = w.hit(addr, unit)
                want = [E("owner", ecx=game, edx=unit), E("skill", unit), E("level", unit)]
                for n, i in enumerate(range(0, bound // 4, par)):
                    want.append(EC("create", game, ANY, *arg_words(
                        [(F_FLAGS, 2), (F_OWNER, owner), (F_MISSILE, unit), (F_TXT, 14 if addr == 0x5ABC40 else 0), (F_SKILL, 0x20),
                         (F_LEVEL, 6), (F_UX, xs[i]), (F_UY, ys[i])])))
                    if made:
                        want += [E("set_tx", news[n], xs[i]), E("set_ty", news[n], ys[i])]
                check(f"{label} class {cls} made {made}", got, base_flag_hs, want)
        w.new_missiles[:] = []
        no_owner = w.unit(cls=8)
        check(f"{label} no owner", w.hit(addr, no_owner), 1, [E("owner", ecx=game, edx=no_owner)])
        check(f"{label} hs1<0", w.hit(addr, w.unit(cls=11)), 1, [])
        check(f"{label} null", w.hit(addr, 0), 1, [])
        for unit in bad_units():
            check(f"{label} bad class", w.hit(addr, unit), 1, [])

    @case("5abc40")
    def _():
        """Check FUN_005abc40 against its model."""
        fan("5abc40", 0x5ABC40, 3, "spread_x", "spread_y", 64, 0x100)

    @case("5ac940")
    def _():
        """Check FUN_005ac940 against its model."""
        fan("5ac940", 0x5AC940, 1, "8dir_x", "8dir_y", 8, 0x20)

    # -------- 005abd70 : life leech to the owner -------------------------------------------
    @case("5abd70")
    def _():
        """Check FUN_005abd70 against its model."""
        for collide in (0, 1):
            mk(12, collide=collide)
            for dmg, cur, mx in ((50, 100, 400), (-30, 100, 400), (0, 100, 400), (500, 100, 400), (300, 100, 400)):
                owner = w.unit(kind=0)
                tgt = w.unit(kind=1)
                unit = w.unit(cls=12, owner=owner)
                w.script["elem_dmg"] = dmg
                w.script["max_hp"] = mx
                w.val["stat"][(owner, 6)] = cur
                got = w.hit(0x5ABD70, unit, tgt)
                new = min(cur + max(dmg, 0), mx) if cur + max(dmg, 0) < mx else mx
                want = [E("owner", ecx=game, edx=unit), E("elem_dmg", tgt, "L", ecx=game, edx=unit),
                        E("max_hp", owner), E("stat", owner, 6, 0), E("set_stat", owner, 6, new, 0)]
                check(f"5abd70 dmg {dmg}", got, 2 + collide, want)
        owner = w.unit()
        unit = w.unit(cls=12, owner=owner)
        check("5abd70 no target", w.hit(0x5ABD70, unit, 0), 1, [E("owner", ecx=game, edx=unit)])
        check("5abd70 no owner", w.hit(0x5ABD70, w.unit(cls=12), w.unit()), 1, [E("owner", ecx=game, edx=ANY)])
        check("5abd70 null", w.hit(0x5ABD70, 0, w.unit()), 1, [])
        for unit in bad_units():
            check("5abd70 bad class", w.hit(0x5ABD70, unit, w.unit()), 1, [])
        w.script.pop("elem_dmg", None)
        w.script.pop("max_hp", None)

    # -------- portal / object hit functions --------------------------------------------------
    @case("5abe50")
    def _():
        """Check FUN_005abe50 against its model."""
        mk(13, p4=0x1F)
        for tx, tgt in ((0, 0), (5, 0), (0, 1)):
            unit = w.unit(cls=13, tx=tx)
            target = w.unit() if tgt else 0
            got = w.hit(0x5ABE50, unit, target)
            want = []
            if not tgt:
                want.append(E("tx", unit))
                if tx == 0:
                    want.append(E("portal", 0x1F, ecx=game, edx=unit))
            check(f"5abe50 tx {tx} target {tgt}", got, 0, want)
        for unit in bad_units() + [0]:
            check("5abe50 bad", w.hit(0x5ABE50, unit, 0), 0, [])

    @case("5abeb0")
    def _():
        """Check FUN_005abeb0 against its model."""
        for room, tgt in ((0x1000, 0), (0, 0), (0x1000, 1)):
            unit = w.unit(room=room)
            got = w.run(0x5ABEB0, ecx=game, edx=unit, args=(w.unit() if tgt else 0,))
            want = []
            if not tgt:
                want.append(E("room", unit))
                if room:
                    want.append(E("portal_flag", room, 1))
            check(f"5abeb0 room {room} target {tgt}", got, 0, want)

    @case("5abee0")
    def _():
        """Check FUN_005abee0 against its model."""
        for obj_mode, line, room, tgt in ((0, 0x3000, 0x1000, 0), (0, 0, 0, 0), (1, 0x3000, 0x1000, 0),
                                          (0, 0x3000, 0x1000, 1), (0, 0, 0x1000, 0)):
            obj = w.unit(kind=2, cls=7, mode=obj_mode, room=room)
            w.val["find"][(2, 0x123)] = obj
            unit = w.unit(tx=0x123)
            m.w32(game + 0xA8, 5000)
            row = m.alloc(0xF0)
            m.w32(row + 0xDC, 0x2A00)
            w.script["obj_line"] = row if line else 0
            got = w.hit(0x5ABEE0, unit, w.unit() if tgt else 0)
            if tgt:
                check("5abee0 target", got, 0, [])
                continue
            want = [E("tx", unit), EC("find", game, 2, *(0x123,)), E("room", obj)]
            if room:
                want.append(E("portal_flag", room, 1))
            if obj_mode == 0:
                want.append(E("update_flag", obj, 1))
                want.append(E("obj_line", 7))
                if line:
                    want.append(E("set_event", 1, (0x2A00 >> 8) + 5000, 0, 0, ecx=game, edx=obj))
            check(f"5abee0 mode {obj_mode} line {line}", got, 1, want)
        # no object found
        unit = w.unit(tx=0x999)
        check("5abee0 no object", w.hit(0x5ABEE0, unit, 0), 0,
              [E("tx", unit), EC("find", game, 2, *(0x999,))])
        w.script.pop("obj_line", None)

    @case("5ac020")
    def _():
        """Check FUN_005ac020 against its model."""
        for tgt, want in ((0, 0), (1, 2), (0x12345678, 2)):
            check(f"5ac020 {tgt}", w.run(0x5AC020, ecx=game, edx=w.unit(), args=(tgt,)), want, [])

    # -------- 005ac0a0 / 005ac250 : counts from Missiles.txt or the skill calc ------------------
    def counted(label, addr, ret_kind):
        """Shared model for the star and scatter hit functions."""
        mk(14, hs1=22, hp1=2, hp2=3)
        mk(15, hs1=22, hp1=0, hp2=0)
        mk(16, hs1=-1)
        w.srow(9, calc3=0x2468)
        for cls, calc, skill, lvl in ((14, 0, 9, 4), (15, 33, 9, 4), (15, -4, 9, 4), (15, 0, 9, 1),
                                      (15, 33, NSKILLS + 2, 4), (15, 33, -1, 4)):
            w.script["skill_calc"] = calc
            owner = w.unit(kind=0)
            unit = w.unit(cls=cls, owner=owner, skill=skill, level=lvl, pos=(41, 42))
            got = w.hit(addr, unit)
            hp1, hp2 = (2, 3) if cls == 14 else (0, 0)
            count = (lvl - 1) * hp2 + hp1
            if ret_kind == "star":
                want = [E("owner", ecx=game, edx=unit), E("unit_x", ecx=unit), E("unit_y", ecx=unit),
                        E("skill", unit), E("level", unit), E("level", unit)]
                ok = True
                if count <= 0:
                    want += [E("skill", unit), E("level", unit)]
                    if 0 <= skill < NSKILLS:
                        want.append(E("skill_calc", owner, 0x2468, skill, lvl))
                        count = max(calc, 1)
                    else:
                        ok = False
                if ok:
                    want.append(E("star", "L", ecx=game, edx=count))
                check(f"{label} {cls} {calc} {skill}", got, 1, want)
            else:
                want = [E("owner", ecx=game, edx=unit), E("level", unit)]
                ok = True
                if count <= 0:
                    want.append(E("skill", unit))
                    if 0 <= skill < NSKILLS:
                        want.append(E("skill_calc", owner, 0x2468, skill, lvl))
                        count = max(calc, 1)
                    else:
                        ok = False
                if ok:
                    want += [E("skill", unit), E("owner", ecx=game, edx=unit),
                             E("scatter", owner, 22, count, count >> 2, skill, lvl, ecx=game, edx=unit)]
                check(f"{label} {cls} {calc} {skill}", got, 1, want)
        check(f"{label} no owner", w.hit(addr, w.unit(cls=14)), 1, [E("owner", ecx=game, edx=ANY)])
        check(f"{label} hs1<0", w.hit(addr, w.unit(cls=16)), 1, [])
        check(f"{label} null", w.hit(addr, 0), 1, [])
        for unit in bad_units():
            check(f"{label} bad class", w.hit(addr, unit), 1, [])
        w.script.pop("skill_calc", None)

    @case("5ac0a0")
    def _():
        """Check FUN_005ac0a0 against its model."""
        counted("5ac0a0", 0x5AC0A0, "star")

    @case("5ac250")
    def _():
        """Check FUN_005ac250 against its model."""
        counted("5ac250", 0x5AC250, "scatter")

    # -------- 005ac1d0 : random monster at the hit --------------------------------------------
    @case("5ac1d0")
    def _():
        """Check FUN_005ac1d0 against its model."""
        for kind, path in ((3, True), (3, False), (2, True), (4, True), (5, True), (0, True)):
            unit = w.unit(kind=kind, path=path, pos=(311, 422), room=0x2222)
            pth = w.units[unit]
            want = []
            if kind in (2, 4, 5):
                x, y = 311, 422
            elif pth == 0:
                x = y = 0
            else:
                x, y = 311, 422
                want += [E("path_y", pth), E("path_x", pth)]
            want += [E("room", unit), E("rand_monster", x, y, ecx=game, edx=0x2222)]
            check(f"5ac1d0 type {kind}", w.hit(0x5AC1D0, unit), 1, want)

    # -------- 005ac350 : heal ----------------------------------------------------------------------
    @case("5ac350")
    def _():
        """Check FUN_005ac350 against its model."""
        for collide in (0, 1):
            mk(17, collide=collide, overlay=0x30)
            mk(18, collide=collide, overlay=0)
            for cls in (17, 18):
                for cur, mx, lo, hi, rnd in ((10, 100, 20, 50, 7), (100, 100, 20, 50, 7), (90, 100, 20, 50, 40),
                                             (0, 100, 5, 5, 0)):
                    w.script["min_dmg"], w.script["max_dmg"], w.script["seed_range"] = lo, hi, rnd
                    w.script["max_hp"] = mx
                    owner = w.unit(kind=0)
                    tgt = w.unit(kind=1)
                    unit = w.unit(cls=cls, owner=owner, skill=4, level=3, seed=(5, 6))
                    w.val["stat"][(tgt, 6)] = cur
                    got = w.hit(0x5AC350, unit, tgt)
                    want = [E("skill", unit), E("level", unit), E("owner", ecx=game, edx=unit),
                            E("min_dmg", owner, 4, 3, 1), E("owner", ecx=game, edx=unit),
                            E("max_dmg", owner, 4, 3, 1), E("seed_range", ecx=unit + 0x20, edx=hi - lo),
                            E("stat", tgt, 6, 0), E("max_hp", tgt)]
                    if cur != mx and cls == 17:
                        want.append(E("overlay", tgt, 0x30, 0))
                    want.append(E("set_stat", tgt, 6, min(cur + rnd + lo, mx), 0))
                    check(f"5ac350 c{collide} cls {cls} cur {cur}", got, collide, want)
        mk(17, collide=0)
        check("5ac350 no target", w.hit(0x5AC350, w.unit(cls=17), 0), 1, [])
        check("5ac350 null", w.hit(0x5AC350, 0, w.unit()), 1, [])
        for unit in bad_units():
            check("5ac350 bad class", w.hit(0x5AC350, unit, w.unit()), 1, [])
        for skill in (-1, NSKILLS):
            unit = w.unit(cls=17, skill=skill, level=1)
            check(f"5ac350 skill {skill}", w.hit(0x5AC350, unit, w.unit()), 1, [E("skill", unit), E("level", unit)])
        for key in ("min_dmg", "max_dmg", "seed_range", "max_hp"):
            w.script.pop(key, None)

    # -------- 005ac480 : fixed star ---------------------------------------------------------------------
    @case("5ac480")
    def _():
        """Check FUN_005ac480 against its model."""
        mk(19, hs1=3, hp1=6, hp2=0)
        mk(20, hs1=3, hp1=6, hp2=2)
        mk(21, hs1=3, hp1=0)
        mk(22, hs1=-1, hp1=3)
        for cls in (19, 20):
            owner = w.unit(kind=0)
            unit = w.unit(cls=cls, owner=owner, skill=12, level=5, pos=(9, 8))
            got = w.hit(0x5AC480, unit)
            want = [E("unit_x", ecx=unit), E("unit_y", ecx=unit), E("skill", unit), E("owner", ecx=game, edx=unit),
                    E("level", unit), ("star", game, 6, "L")]
            check(f"5ac480 class {cls}", got, 1, want)
        for cls in (21, 22):
            check(f"5ac480 class {cls}", w.hit(0x5AC480, w.unit(cls=cls)), 1, [])
        check("5ac480 null", w.hit(0x5AC480, 0), 1, [])
        for unit in bad_units():
            check("5ac480 bad class", w.hit(0x5AC480, unit), 1, [])

    # -------- 005ac800 : block until the missile is old enough -----------------------------------------
    @case("5ac800")
    def _():
        """Check FUN_005ac800 against its model."""
        mk(23, hp1=10)
        for rem, tot, tgt in ((50, 100, 1), (89, 100, 1), (90, 100, 1), (91, 100, 1), (0, 5, 1), (5, 100, 0), (-3, 4, 1)):
            unit = w.unit(cls=23, remaining=rem, total=tot)
            got = w.hit(0x5AC800, unit, w.unit() if tgt else 0)
            want = [E("remaining", unit), E("total", unit)] if tgt else []
            check(f"5ac800 {rem}/{tot}", got, 2 if tgt and rem < tot - 10 else 0, want)
        check("5ac800 null", w.hit(0x5AC800, 0, 0), 1, [])
        for unit in bad_units():
            check("5ac800 bad class", w.hit(0x5AC800, unit, 0), 1, [])

    # -------- 005ac870 : up to three sub-missiles ------------------------------------------------------------
    @case("5ac870")
    def _():
        """Check FUN_005ac870 against its model."""
        for hs2, hs3 in ((-1, -1), (4, -1), (-1, 5), (4, 5), (0, 0)):
            mk(24, hs1=3, hs2=hs2, hs3=hs3)
            owner = w.unit(kind=1)
            unit = w.unit(cls=24, owner=owner, skill=3, level=2)
            got = w.hit(0x5AC870, unit)
            want = [E("owner", ecx=game, edx=unit), E("skill", unit), E("level", unit)]
            ids = [3] + ([hs2] if hs2 >= 0 else []) + ([hs3] if hs3 >= 0 else [])
            for ident in ids:
                want.append(EC("create", game, ANY, *arg_words(
                    [(F_OWNER, owner), (F_MISSILE, unit), (F_TXT, ident), (F_SKILL, 3), (F_LEVEL, 2)])))
            check(f"5ac870 {hs2} {hs3}", got, 1, want)
        mk(25, hs1=-1)
        check("5ac870 hs1<0", w.hit(0x5AC870, w.unit(cls=25)), 1, [])
        check("5ac870 null", w.hit(0x5AC870, 0), 1, [])
        for unit in bad_units():
            check("5ac870 bad class", w.hit(0x5AC870, unit), 1, [])

    # -------- 005aca50 : druid aura -----------------------------------------------------------------------------
    @case("5aca50")
    def _():
        """Check FUN_005aca50 against its model."""
        m.w32(game + 0xA8, 1000)
        for tx, length, tgt, owner_ok in ((1012, 40, 1, 1), (1009, 40, 1, 1), (1010, 10, 1, 1), (1010, 9, 1, 1),
                                          (1100, 99, 1, 1), (1012, 40, 0, 1), (1012, 40, 1, 0), (900, 40, 1, 1)):
            w.script["elem_len"] = length
            owner = w.unit(kind=0) if owner_ok else 0
            unit = w.unit(owner=owner, skill=8, level=3, tx=tx)
            target = w.unit(kind=1) if tgt else 0
            got = w.run(0x5ACA50, ecx=game, edx=unit, args=(target,))
            want = [E("owner", ecx=game, edx=unit)]
            ret = 1
            if owner_ok and tgt:
                dur = tx - 1000
                want += [E("skill", unit), E("level", unit), E("tx", unit)]
                if dur >= 10:
                    want.append(E("elem_len", owner, 8, 3, 1))
                    if dur <= length:
                        want.append(E("aura", target, dur, 8, 3, ecx=game, edx=owner))
                        ret = 2
            check(f"5aca50 {tx} {length} {tgt} {owner_ok}", got, ret, want)
        w.script.pop("elem_len", None)

    # -------- 005acaf0 : a monster from the skill's param 1 ----------------------------------------------------------
    @case("5acaf0")
    def _():
        """Check FUN_005acaf0 against its model."""
        for case_ in ("ok", "target", "no_owner", "no_skill"):
            owner = w.unit(kind=0) if case_ != "no_owner" else 0
            unit = w.unit(owner=owner, skill=4, pos=(61, 62), room=0x3333)
            w.script["skill_by_enum"] = 0x7000 if case_ != "no_skill" else 0
            w.script["param1"] = 0x99
            target = w.unit() if case_ == "target" else 0
            got = w.run(0x5ACAF0, ecx=game, edx=unit, args=(target,))
            want = []
            if case_ != "target":
                want.append(E("owner", ecx=game, edx=unit))
                if case_ not in ("no_owner",):
                    want += [E("skill", unit), E("skill_by_enum", ecx=owner, edx=4)]
                    if case_ == "ok":
                        want += [E("param1", 0x7000), E("unit_y", ecx=unit), E("unit_x", ecx=unit), E("room", unit),
                                 E("spawn", 0x99, 61, 62, 1, ecx=game, edx=0x3333)]
            check(f"5acaf0 {case_}", got, 1, want)
        for key in ("skill_by_enum", "param1"):
            w.script.pop(key, None)

    # -------- 005acb60 : drain mana percent -----------------------------------------------------------------------------
    @case("5acb60")
    def _():
        """Check FUN_005acb60 against its model."""
        for hp1 in (-3, 0, 1, 2, 30, 99, 100, 250):
            mk(26, hp1=hp1)
            for cur in (-2, 0, 1, 2, 3, 7, 100, 1000):
                tgt = w.unit(kind=0)
                w.val["stat"][(tgt, 8)] = cur
                unit = w.unit(cls=26)
                got = w.hit(0x5ACB60, unit, tgt)
                if hp1 <= 0:
                    check(f"5acb60 hp1 {hp1}", got, 1, [])
                    continue
                want = [E("stat", tgt, 8, 0)]
                if cur <= 0:
                    check(f"5acb60 hp1 {hp1} cur {cur}", got, 1, want)
                    continue
                pct = 1 if hp1 <= 1 else min(hp1, 100)
                drain = percent(cur, pct)
                low = max(drain, 1)
                take = cur if low >= cur else low
                want += [E("percent", cur, pct, 100, ecx=cur, edx=pct), E("set_stat", tgt, 8, cur - take, 0)]
                want[1] = ("percent", cur, pct, 100)
                check(f"5acb60 hp1 {hp1} cur {cur}", got, 2, want)
        mk(26, hp1=5)
        check("5acb60 monster", w.hit(0x5ACB60, w.unit(cls=26), w.unit(kind=1)), 2, [])
        check("5acb60 no target", w.hit(0x5ACB60, w.unit(cls=26), 0), 2, [])
        check("5acb60 null", w.hit(0x5ACB60, 0, w.unit(kind=0)), 1, [])
        for unit in bad_units():
            check("5acb60 bad class", w.hit(0x5ACB60, unit, w.unit(kind=0)), 1, [])

    # -------- 005acf20 / 005acfc0 ---------------------------------------------------------------------------------------------
    @case("5acf20")
    def _():
        """Check FUN_005acf20 against its model."""
        for hp1 in (-2, 0, 4):
            mk(27, hs1=17, hp1=hp1)
            owner = w.unit(kind=1)
            unit = w.unit(cls=27, owner=owner, skill=6, level=2)
            got = w.hit(0x5ACF20, unit)
            want = [E("owner", ecx=game, edx=unit), E("level", unit), E("skill", unit),
                    E("radial", unit, 17, 6, 2, max(hp1, 1), 2, 0, ecx=game, edx=owner)]
            check(f"5acf20 hp1 {hp1}", got, 1, want)
        mk(28, hs1=-1)
        check("5acf20 no owner", w.hit(0x5ACF20, w.unit(cls=27)), 1, [E("owner", ecx=game, edx=ANY)])
        check("5acf20 hs1<0", w.hit(0x5ACF20, w.unit(cls=28)), 1, [])
        check("5acf20 null", w.hit(0x5ACF20, 0), 1, [])
        for unit in bad_units():
            check("5acf20 bad class", w.hit(0x5ACF20, unit), 1, [])

    @case("5acfc0")
    def _():
        """Check FUN_005acfc0 against its model."""
        mk(29, hp1=0x42)
        for skill, lvl, tgt, owner_ok in ((5, 3, 0, 1), (5, 3, 1, 1), (0, 3, 0, 1), (5, 0, 0, 1), (-1, 3, 0, 1), (5, 3, 1, 0)):
            owner = w.unit(kind=1) if owner_ok else 0
            unit = w.unit(cls=29, owner=owner, skill=skill, level=lvl)
            target = w.unit() if tgt else 0
            got = w.hit(0x5ACFC0, unit, target)
            want = [E("skill", unit), E("level", unit)]
            if skill > 0 and lvl > 0:
                want.append(E("owner", ecx=game, edx=unit))
                if owner_ok:
                    if tgt:
                        want.append(E("set_target", unit, target))
                    want.append(E("dispatch", 0x42, skill, lvl, ecx=game, edx=unit))
            check(f"5acfc0 {skill} {lvl} {tgt} {owner_ok}", got, 1, want)
        check("5acfc0 null", w.hit(0x5ACFC0, 0, 0), 1, [])
        for unit in bad_units():
            check("5acfc0 bad class", w.hit(0x5ACFC0, unit, 0), 1, [])

    # -------- damage functions -------------------------------------------------------------------------------------------------------
    def calc_pair(addr, label, with_clear):
        """FUN_005ad050 (move) and FUN_005ad0f0 (recalculate): the conversion percentage calc."""
        mk(30, calc=0x0F0F)
        for pct_result, phys in ((0, 100), (-5, 100), (10, 100), (99, 100), (100, 100), (150, 100), (50, 0), (33, 7)):
            w.script["eval_calc"] = pct_result
            w.script["elem_dmg"] = 6
            owner = w.unit(kind=0)
            unit = w.unit(cls=30, owner=owner)
            tgt = w.unit(kind=1)
            dmg = w.damage(o8=phys)
            got = w.dmg(addr, unit, tgt, dmg)
            want = [E("owner", ecx=game, edx=unit), E("eval_calc", unit, owner, 0x0F0F, 0xFFFFFFFF, 0xFFFFFFFF)]
            if pct_result > 0:
                pct = min(pct_result, 100)
                moved = min(percent(phys, pct), phys)
                want.append(("percent", phys, pct, 100))
                if with_clear:
                    want += [E("elem_dmg", tgt, dmg, ecx=game, edx=unit), E("clear_dmg", dmg, ecx=game, edx=unit),
                             E("add_elem", tgt, dmg, moved + 6, ecx=game, edx=unit)]
                    final = phys
                else:
                    want.append(E("add_elem", tgt, dmg, moved, ecx=game, edx=unit))
                    final = phys - moved
            else:
                final = phys
            check(f"{label} {pct_result}/{phys}", got, got[0], want)
            require(s32(m.r32(dmg + 8)) == final, f"{label}: physical {s32(m.r32(dmg + 8))} want {final}")
        for unit in bad_units() + [0]:
            dmg = w.damage(o8=9)
            result = w.dmg(addr, unit, w.unit(), dmg)
            require(result[1] == [] and s32(m.r32(dmg + 8)) == 9, f"{label}: guard")
        for key in ("eval_calc", "elem_dmg"):
            w.script.pop(key, None)

    @case("5ad050")
    def _():
        """Check FUN_005ad050 against its model."""
        calc_pair(0x5AD050, "5ad050", False)

    @case("5ad0f0")
    def _():
        """Check FUN_005ad0f0 against its model."""
        calc_pair(0x5AD0F0, "5ad0f0", True)

    @case("5ad1b0")
    def _():
        """Check FUN_005ad1b0 against its model."""
        for dp1 in (-9, 0, 25, 100, 300):
            for cold in (0, 7, 100):
                mk(31, dp1=dp1)
                unit = w.unit(cls=31)
                w.val["stat"][(unit, 0x38)] = cold
                dmg = w.damage(o30=77, o34=88)
                got = w.dmg(0x5AD1B0, unit, 0, dmg)
                pct = max(dp1, 0)
                check(f"5ad1b0 {dp1}/{cold}", got, got[0], [("stat", ANY, ANY, unit, 0x38, 0), ("percent", cold, pct, 100)])
                require(s32(m.r32(dmg + 0x34)) == percent(cold, pct) and m.r32(dmg + 0x30) == 0, "5ad1b0 fields")

    @case("5ad220")
    def _():
        """Check FUN_005ad220 against its model."""
        mk(32, dp1=40)
        mk(33, dp1=0)
        mk(34, dp1=200)
        for cls in (32, 33, 34):
            for seed in ((1, 2), (0x12345678, 0x9ABCDEF0), (0xFFFFFFFF, 0xFFFFFFFF), (7, 0)):
                unit = w.unit(cls=cls, seed=seed)
                dmg = w.damage(o4=0x0102)
                w.dmg(0x5AD220, unit, 0, dmg)
                prod = seed[0] * 0x6AC690C5 + seed[1]
                lo, hi = prod & 0xFFFFFFFF, prod >> 32
                require((m.r32(unit + 0x20), m.r32(unit + 0x24)) == (lo, hi), "5ad220 seed")
                want = 0x0102 | (0x4000 if (lo & 0x7F) < s32(m.r32(w.mrows + cls * 0x1A4 + 0x78)) else 0)
                require(m.r16(dmg + 4) == want, f"5ad220 flags {m.r16(dmg + 4):#x} want {want:#x}")
        for unit in bad_units() + [0]:
            dmg = w.damage(o4=3)
            w.dmg(0x5AD220, unit, 0, dmg)
            require(m.r16(dmg + 4) == 3, "5ad220 guard")

    @case("5ad290")
    def _():
        """Check FUN_005ad290 against its model."""
        dmg = w.damage(o30=0x55, o34=0x11)
        w.dmg(0x5AD290, 0, 0, dmg)
        require((m.r32(dmg + 0x30), m.r32(dmg + 0x34)) == (0, 0x55), "5ad290")

    @case("5ad2b0")
    def _():
        """Check FUN_005ad2b0 against its model."""
        mk(35)
        dmg = w.damage(o30=0x55, o34=0x11)
        w.dmg(0x5AD2B0, w.unit(cls=35), 0, dmg)
        require((m.r32(dmg + 0x30), m.r32(dmg + 0x34)) == (0x55, 0x55), "5ad2b0")
        for unit in bad_units() + [0]:
            dmg = w.damage(o30=1, o34=2)
            w.dmg(0x5AD2B0, unit, 0, dmg)
            require(m.r32(dmg + 0x34) == 2, "5ad2b0 guard")

    @case("5ad3a0")
    def _():
        """Check FUN_005ad3a0 against its model."""
        for owner_flags, owner_ok, tgt in ((0, 1, 1), (0x200, 1, 1), (0x1FF, 1, 1), (0, 0, 1), (0, 1, 0)):
            owner = w.unit(kind=0, flags=owner_flags) if owner_ok else 0
            unit = w.unit(owner=owner)
            target = w.unit(kind=1, flags=0x10) if tgt else 0
            got = w.dmg(0x5AD3A0, unit, target, w.damage())
            check(f"5ad3a0 {owner_flags:x}", got, got[0], [E("owner", ecx=game, edx=unit)])
            if tgt:
                want = 0x10 | (0x20000 if owner_ok and not (owner_flags >> 9) & 1 else 0)
                require(m.r32(target + 0xC4) == want, "5ad3a0 flags")

    @case("5ad3e0")
    def _():
        """Check FUN_005ad3e0 against its model."""
        for dp1 in (-1, 0, 1, 200):
            mk(36, dp1=dp1)
            w.script["range"] = 0x77
            unit = w.unit(cls=36, skill=4, level=2)
            dmg = w.damage(o44=1, o60=2)
            got = w.dmg(0x5AD3E0, unit, 0, dmg)
            want = [E("skill", unit), E("level", unit)]
            if dp1 <= 0:
                want.append(E("range", ecx=4, edx=2))
            check(f"5ad3e0 {dp1}", got, got[0], want)
            require((m.r32(dmg + 0x44), m.r32(dmg + 0x60)) == (dp1 if dp1 > 0 else 0x77, 0x60), "5ad3e0 fields")
        w.script.pop("range", None)

    @case("5ad450")
    def _():
        """Check FUN_005ad450 against its model."""
        mk(37, overlay=0x21)
        mk(38, overlay=0)
        mk(39, overlay=-4)
        for cls in (37, 38, 39):
            tgt = w.unit(kind=1)
            got = w.dmg(0x5AD450, w.unit(cls=cls), tgt, w.damage())
            check(f"5ad450 {cls}", got, got[0], [E("overlay", tgt, 0x21, 0)] if cls == 37 else [])
        require(w.dmg(0x5AD450, w.unit(cls=37), 0, w.damage())[1] == [], "5ad450 no target")
        require(w.dmg(0x5AD450, 0, w.unit(), w.damage())[1] == [], "5ad450 null")
        for unit in bad_units():
            require(w.dmg(0x5AD450, unit, w.unit(), w.damage())[1] == [], "5ad450 bad class")

    @case("5ad4a0")
    def _():
        """Check FUN_005ad4a0 against its model."""
        w.srow(2, param1=0x2222)
        for dp1, skill in ((5, 2), (0, 2), (-1, 2), (0, -1), (0, NSKILLS), (0, NSKILLS + 7), (9, -1)):
            mk(39, dp1=dp1)
            unit = w.unit(cls=39, skill=skill)
            dmg = w.damage(o44=1, o60=0)
            got = w.dmg(0x5AD4A0, unit, 0, dmg)
            expect_stun = dp1 if dp1 > 0 else (0x2222 if 0 <= skill < NSKILLS and skill == 2 else 0)
            if dp1 <= 0 and 0 <= skill < NSKILLS and skill != 2:
                expect_stun = 0
            require((m.r32(dmg + 0x44), m.r32(dmg + 0x60)) == (expect_stun, 0x60), f"5ad4a0 {dp1}/{skill}")
            check(f"5ad4a0 {dp1}/{skill}", got, got[0], [E("skill", unit)])
        for unit in bad_units() + [0]:
            dmg = w.damage(o44=1)
            require(w.dmg(0x5AD4A0, unit, 0, dmg)[1] == [] and m.r32(dmg + 0x44) == 1, "5ad4a0 guard")

    @case("5ad530")
    def _():
        """Check FUN_005ad530 against its model."""
        m.w32(game + 0xA8, 500)
        for tx, length, owner_ok, tgt in ((520, 40, 1, 1), (509, 40, 1, 1), (510, 40, 1, 1), (550, 40, 1, 1),
                                          (540, 40, 1, 1), (541, 40, 1, 1), (520, 40, 0, 1), (520, 40, 1, 0)):
            w.script["elem_len"] = length
            owner = w.unit(kind=0) if owner_ok else 0
            unit = w.unit(owner=owner, skill=3, level=4, tx=tx)
            dmg = w.damage(o28=0x33, o2c=0x44)
            got = w.dmg(0x5AD530, unit, w.unit(kind=1) if tgt else 0, dmg)
            want = [E("owner", ecx=game, edx=unit)]
            ok = False
            if owner_ok and tgt:
                want += [E("skill", unit), E("level", unit), E("tx", unit)]
                d = tx - 500
                if d >= 10:
                    want.append(E("elem_len", owner, 3, 4, 1))
                    ok = d <= length
            check(f"5ad530 {tx} {owner_ok} {tgt}", got, got[0], want)
            if ok:
                require((m.r32(dmg + 0x28), m.r32(dmg + 0x2C)) == (0x33, tx - 500), "5ad530 kept")
            else:
                require((m.r32(dmg + 0x28), m.r32(dmg + 0x2C)) == (0, 0), "5ad530 cleared")
        w.script.pop("elem_len", None)

    @case("5ad5c0")
    def _():
        """Check FUN_005ad5c0 against its model."""
        for kind in (1, 0, 2):
            for pct, phys in ((25, 100), (0, 100), (-30, 77), (25, -9), (180, 3), (7, 1000000)):
                owner = w.unit(kind=0)
                unit = w.unit(owner=owner)
                tgt = w.unit(kind=kind)
                w.val["stat"][(owner, 0x19)] = pct
                dmg = w.damage(o8=phys)
                got = w.dmg(0x5AD5C0, unit, tgt, dmg)
                if kind != 1:
                    check(f"5ad5c0 kind {kind}", got, got[0], [])
                    continue
                check(f"5ad5c0 {pct}/{phys}", got, got[0], [E("owner", ecx=game, edx=unit), E("stat", owner, 0x19, 0),
                                                           E("undead_bonus", tgt, dmg, ecx=game, edx=unit)])
                require(s32(m.r32(dmg + 8)) == tdiv(pct * phys, 100) + phys, f"5ad5c0 damage {pct}/{phys}")
        no_owner = w.unit()
        dmg = w.damage(o8=40)
        tgt = w.unit(kind=1)
        require(w.dmg(0x5AD5C0, no_owner, tgt, dmg)[1][-1][0] == "undead_bonus" and s32(m.r32(dmg + 8)) == 40, "5ad5c0 no owner")
        require(w.dmg(0x5AD5C0, w.unit(), 0, w.damage())[1] == [], "5ad5c0 no target")

    @case("5ad630")
    def _():
        """Check FUN_005ad630 against its model."""
        mons = {}
        for cls_id in range(4):
            mons[cls_id] = w.unit(kind=1, cls=cls_id)
        flags = {0: (0, 0), 1: (1, 0), 2: (0, 1), 3: (1, 1)}   # monster class -> (small, large)
        for cls_id, (small, large) in flags.items():
            w.val["monflag"][(cls_id, 10)] = small
            w.val["monflag"][(cls_id, 11)] = large
        for dp1 in (-2, 0, 1, 2, 5):
            for dp2 in (-5, 0, 10, 30):
                for kind, mclass in [(0, 0)] + [(1, c) for c in range(4)] + [(2, 0), (4, 0)]:
                    for rnd in (0, 9, 10, 29, 30, 31, 59, 60, 99):
                        w.script["seed_mod"] = rnd
                        tgt = w.unit(kind=kind, cls=mclass)
                        unit = w.unit(cls=39, seed=(3, 4))
                        mk(39, dp1=dp1, dp2=dp2)
                        dmg = w.damage(o4=0)
                        got = w.dmg(0x5AD630, unit, tgt, dmg)
                        want = []
                        chance = None
                        if kind == 0:
                            chance = dp2 if dp1 >= 1 else None
                        elif kind == 1:
                            small, large = flags[mclass]
                            if dp1 < 1:
                                want.append(("monflag", mclass, 10))
                                chance = dp2 if small else None
                            elif dp1 == 1:
                                want.append(("monflag", mclass, 10))
                                if small:
                                    chance = dp2 * 2
                                else:
                                    want.append(("monflag", mclass, 11))
                                    chance = None if large else dp2
                            else:
                                want.append(("monflag", mclass, 10))
                                if small:
                                    chance = dp2 * 3
                                else:
                                    want.append(("monflag", mclass, 11))
                                    chance = dp2 if large else dp2 * 2
                        rolled = chance is not None and chance > 0
                        if rolled:
                            want.append(("seed_mod", unit + 0x20, 100))
                        log = [(e[0],) + tuple(e[1:]) for e in got[1]]
                        names = [e[0] for e in want]
                        require([e[0] for e in log] == names, f"5ad630 calls {[e[0] for e in log]} want {names}")
                        for entry, wanted in zip(log, want):
                            if wanted[0] == "monflag":
                                require(entry[1:3] == (wanted[1], wanted[2]), "5ad630 monflag regs")
                            else:
                                require(entry[1:3] == (unit + 0x20, 100), "5ad630 seed regs")
                        expect = 8 if rolled and rnd < chance else 0
                        require(m.r16(dmg + 4) == expect, f"5ad630 dp {dp1}/{dp2} kind {kind} class {mclass} rnd {rnd}: flags {m.r16(dmg + 4)} want {expect}")
        w.script.pop("seed_mod", None)
        missile_row = 39
        mk(missile_row, dp1=1, dp2=50)
        for unit in (w.unit(kind=1, cls=missile_row), w.unit(kind=3, cls=-1), w.unit(kind=3, cls=NROWS), 0):
            dmg = w.damage()
            require(w.dmg(0x5AD630, unit, w.unit(kind=1), dmg)[1] == [], "5ad630 guard")
        require(w.dmg(0x5AD630, w.unit(cls=missile_row), 0, w.damage())[1] == [], "5ad630 no target")

    # -------- redemption callbacks and the Tyrael portal ----------------------------------------------------------------------------------------------------------------------
    @case("5ad8f0")
    def _():
        """Check FUN_005ad8f0 against its model."""
        for fn, flag in ((0x5AD8F0, 0), (0x5AD910, 1)):
            for a, b in ((1, 2), (0x1111, 0x2222)):
                w.script["blizzard"] = 0x31
                got = w.run(fn, args=(a, b, 0x99))
                check(f"{fn:x}", got, 0x31, [E("blizzard", a, 0x7C, b, flag)])
        w.script.pop("blizzard", None)

    @case("5ad970")
    def _():
        """Check FUN_005ad970 against its model."""
        for tx, tgt, quest in ((0, 0, 1), (0, 0, 0), (3, 0, 1), (0, 1, 1)):
            w.script["quest"] = quest
            w.script["room"] = None
            unit = w.unit(tx=tx, room=0x4444)
            got = w.run(0x5AD970, ecx=game, edx=unit, args=(w.unit() if tgt else 0,))
            want = []
            if not tgt:
                want.append(E("tx", unit))
                if tx == 0:
                    want += [E("set_tx", unit, 1), E("room", unit), E("quest", game, 0x24)]
                    if quest:
                        want.append(E("tyrael", unit, ecx=game, edx=0x4444))
                    want.append(E("portal_flag", 0x4444, 1))
            check(f"5ad970 {tx} {tgt} {quest}", got, 0, want)
        w.script.pop("quest", None)

    # -------- 005ad9d0 : expire / free wrapper ---------------------------------------------------------------------------------------------------------------------------------
    @case("5ad9d0")
    def _():
        """Check FUN_005ad9d0 against its model."""
        m.w32(HIT_SIZE, 5)
        m.w32(HIT_TABLE + 4 * 3, w.hit_stub)
        for idx, expire in ((3, 1), (3, 0), (0, 1), (-1, 1), (5, 1), (4, 1), (9, 1), (1, 1)):
            mk(39, hitfunc=idx)
            unit = w.unit(cls=39)
            m.w32(HIT_TABLE + 4 * 1, w.hit_stub)
            m.w32(HIT_TABLE + 4 * 4, w.hit_stub)
            got = w.run(0x5AD9D0, ecx=game, edx=unit, args=(expire,))
            want = []
            if expire and 0 < idx < 5:
                want.append(("hit", game, unit, 0))
            want.append(("free", game, unit))
            check(f"5ad9d0 {idx}/{expire}", got, got[0], want)
        got = w.run(0x5AD9D0, ecx=game, edx=0, args=(0,))
        check("5ad9d0 null", got, got[0], [("free", game, 0)])

    # -------- 005ada80 : pierce counter ----------------------------------------------------------------------------------------------------------------------------------------------------------------
    @case("5ada80")
    def _():
        """Check FUN_005ada80 against its model."""
        for flag_bit, pierce in ((True, 3), (True, 1), (True, 0), (False, 3)):
            mk(39, f4=mask[2] if flag_bit else 0)
            unit = w.unit(cls=39)
            w.val["stat"][(unit, 0x148)] = pierce
            got = w.run(0x5ADA80, regs={"esi": unit})
            want = []
            ret = 3
            if flag_bit:
                want.append(("stat", ANY, ANY, unit, 0x148, 0))
                if pierce:
                    want.append(("set_stat", ANY, ANY, unit, 0x148, pierce - 1, 0))
                    ret = 2
            check(f"5ada80 {flag_bit} {pierce}", got, ret, want)

    # -------- 005adbb0 / 005adcc0 : server do-function dispatcher --------------------------------------------------------------------------------------------------------------------------------
    def owner_in_town(want, owner, owner_room, owner_town):
        """Log the owner's room lookup and, when it has a room, its town test; True when that room is town."""
        want.append(E("room", owner))
        if owner_room == 0:
            return False
        want.append(E("in_town", owner_room))
        return bool(owner_town)

    def srv_do(label, call):
        """Model FUN_005adbb0 over its decision tree; `call` runs the entry point."""
        m.w32(DO_SIZE, 6)
        for idx in range(1, 6):
            m.w32(DO_TABLE + 4 * idx, w.do_stub)
        for dofunc, f5, missile_town, owner_kind, (owner_room, owner_town), ret, room_known in itertools.product(
                (0, -1, 6, 9, 2), range(4), (0, 1), (None, 0, 1), ((0, 0), (0x8100, 0), (0x8100, 1)), (1, 2),
                (True, False)):
            keep_town, block_town = f5 & 1, f5 & 2    # Missiles.txt byte 5: bit 2 stays in town, bit 3 is blocked there
            mk(39, dofunc=dofunc, f5=(mask[2] if keep_town else 0) | (mask[3] if block_town else 0))
            owner = w.unit(kind=owner_kind, room=owner_room) if owner_kind is not None else 0
            unit = w.unit(cls=39, owner=owner, pos=(21, 22), room=0x8000 if room_known else 0)
            w.val["oroom"] = {0x8000: missile_town, 0x8100: owner_town, 0x8200: 1}
            w.script["do"] = ret
            w.script["room_near"] = 0x8200
            got = call(unit)
            want = []
            if 0 < dofunc < 6:
                where = 0x8000 if room_known else 0x8200
                want.append(E("room", unit))
                if not room_known:
                    want += [E("unit_y", ecx=unit), E("unit_x", ecx=unit), E("room_near", 22, ecx=0, edx=21)]
                want.append(E("owner", ecx=game, edx=unit))

                freed = False
                if block_town and owner:
                    freed = owner_in_town(want, owner, owner_room, owner_town)
                if not freed and owner and owner_kind == 0:
                    freed = owner_in_town(want, owner, owner_room, owner_town)
                if not freed and not keep_town:
                    want.append(E("in_town", where))
                    freed = bool(missile_town if room_known else 1)
                if not freed:
                    want.append(("do", game, unit))
                    freed = ret == 2
                if freed:
                    want.append(("free", game, unit))
            require(strip_regs(got[1]) == strip_regs(want),
                    f"{label} dofunc {dofunc} f5 {f5} town {missile_town} owner {owner_kind}/{owner_room}/{owner_town} "
                    f"ret {ret} known {room_known}\n got  {got[1]}\n want {want}")
        w.script.pop("do", None)
        w.script.pop("room_near", None)

    def strip_regs(log):
        """Names and stack arguments only (ecx/edx checked where the model states them)."""
        return [(e[0],) + tuple(e[3:]) for e in log]

    @case("5adbb0")
    def _():
        """Check FUN_005adbb0 against its model."""
        srv_do("5adbb0", lambda unit: w.run(0x5ADBB0, args=(game,), regs={"edi": unit}))

    @case("5adcc0")
    def _():
        """Check FUN_005adcc0 against its model."""
        srv_do("5adcc0", lambda unit: w.run(0x5ADCC0, ecx=game, edx=unit, args=(0,)))

    return cases


def main():
    """Run every MissMode.cpp function against its model."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d (1.14.3.71)")
    world = World(exe)
    cases = build_cases(world)
    for _label, thunk in cases:
        thunk()
    print(f"ok: {len(cases)} MissMode.cpp function groups (44 functions) against stock game.exe")


if __name__ == "__main__":
    main()
