# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the COF path builder and its two wrappers in stock game.exe 1.14d (Composit.cpp).

    uv run python composit_cof.py [--mutation-check]

Runs game.exe's own code for three functions against a hand-written model and
compares the return value, the callee log (arguments in call order), the output
buffer, the weapon-class cell and the esp left by the `ret n`:

  FUN_0064f5b0(unit, class, mode, type, inv, out, &wclass, full, special)
      builds the COF name: type 0 player / 1 monster / 2 object, three tokens
      (type, mode, weapon class) with each blank byte turned into a NUL, then
      "%s%s%s" or "<dir>\\<type>\\COF\\<type><mode><wclass>.COF".
  FUN_0064f380(unit, inv, &index, mode, flag) -> weapon-class token
      the unit's type and class go through the state transform (flag 0x08 in
      unit +0xc8) first; `flag` 0 puts the original mode back.
  FUN_0064fa00(unit, out, &wclass, full, special, inv, mode)
      always runs the state transform, then FUN_0064f5b0; null unit is fatal
      (Composit.cpp line 0x106).

Callees are hooked and logged, not claimed: the weapon-class resolver, the
state transform, the unit mode getter, the three token-line getters, the
monster flag test, and (for FUN_0064fa00) FUN_0064f5b0. wsprintfA is the
emulator's own. Callee addresses are written with an underscore (0x65_b480) so
the gap map counts only the functions this oracle claims. `--mutation-check`
breaks the model on purpose, one defect at a time, and requires each to fail.
No MPQs are read.
"""
import argparse
import hashlib
import random
import struct
import sys
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
BUILD_COF = 0x64F5B0
WEAPON_CLASS = 0x64F380
COF_WITH_TRANSFORM = 0x64FA00
# Hooked callees (not claims).
RESOLVE = 0x64_F060
TRANSFORM = 0x64_5270
ANIM_MODE = 0x62_1190
PLR_LINE = 0x65_B480
MON_LINE = 0x65_B500
OBJ_LINE = 0x65_B570
MON_FLAG = 0x46_38A0
ERROR_LINE = 0x40_8A60
ERROR_IP = 0x40_8090
FATAL_EXIT = 0x68_1E09
# Data.
TABLE_PTR = 0x744304
PLR_OVERRIDES, PLR_OVERRIDE_COUNT = 0x745900, 0x745910
MON_OVERRIDES, MON_OVERRIDE_COUNT = 0x745914, 0x74591C
FILE_NAME = ".\\COMPOSIT\\Composit.cpp"
HTH = 0x20687468
SIX = {0x20736831, 0x20746831, 0x20736A31, 0x20746A31, 0x20737331, 0x20747331}
DIRS = {0: "DATA\\GLOBAL\\CHARS", 1: "DATA\\GLOBAL\\MONSTERS", 2: "DATA\\GLOBAL\\OBJECTS"}
FULL = "%s\\%s\\COF\\%s%s%s.COF"
SHORT = "%s%s%s"
TOKENS = [b"hth ", b"1hs ", b"1ht ", b"2hs ", b"bow ", b"1js ", b"1jt ", b"1ss ", b"1st ", b"xbw ",
          b"ab c", b"  x ", b"a   ", b"gh  ", b"    ", b"abcd", b"1h s"]
MONSTERS = 4
OUT_SIZE = 96
INSTRUCTION_LIMIT = 5000
MUTATIONS = ("player-mode-first", "mon-flag-inverted", "obj-as-monster", "keep-transformed-mode",
             "override-first-wins", "no-mode-b-quirk", "fatal-line", "swap-class-type", "short-keeps-blank")
mutation = None


class Mismatch(Exception):
    """The emulated function and the model disagree."""


class Fatal(Exception):
    """game.exe reached its fatal exit; carries the reported line."""


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def require(condition, message):
    """Stop the probe when an observation is absent or differs."""
    if not condition:
        raise Mismatch(message)


def word(token):
    """A 4-byte token as the dword the tables hold."""
    return struct.unpack("<I", token)[0]


def token_of(table, index, kind):
    """The synthetic token a line getter hands back for (table, index, kind)."""
    return word(TOKENS[(index * 5 + kind * 3 + table * 7) % len(TOKENS)])


def resolve_of(utype, cls, mode):
    """The hooked resolver's token for a unit type, class and mode."""
    return word(TOKENS[(utype * 11 + cls * 3 + mode) % len(TOKENS)]), 100 + (cls + mode) % 7


def trimmed(value):
    """The C string a token becomes: three bytes, each blank turned into NUL, cut at the first NUL."""
    raw = struct.pack("<I", value)[:3]
    return bytes(0 if byte == 0x20 else byte for byte in raw).split(b"\0")[0].decode("latin-1")


class World:
    """One emulated game.exe with its callees hooked and a call log."""

    def __init__(self, exe, hook_build_cof=False):
        """Build the machine, the fake data table and every hook."""
        self.log = []
        self.script = {}
        self.fatal_line = None
        sprintf_original = emu.Emu.w_wsprintfA

        def sprintf(machine):
            """Log wsprintfA's format and string arguments, then format for real."""
            fmt = machine.cstr(machine.arg(1))
            strings = tuple(machine.cstr(machine.arg(2 + i)) for i in range(fmt.count("%s")))
            self.log.append(("sprintf", fmt, strings))
            return sprintf_original(machine)

        emu.Emu.w_wsprintfA = sprintf
        try:
            self.m = emu.Emu(exe)
        finally:
            emu.Emu.w_wsprintfA = sprintf_original
        m = self.m
        self.table = m.alloc(0xB00)
        self.monsters = m.alloc(MONSTERS * 0x1A8)
        m.w32(TABLE_PTR, self.table)
        m.w32(self.table + 0xA78, self.monsters)
        m.w32(self.table + 0xA80, MONSTERS)
        self.stock_player = [(m.read(PLR_OVERRIDES + 8 * i, 4), m.r32(PLR_OVERRIDES + 8 * i + 4)) for i in range(2)]
        self.stock_monster = [(m.read(MON_OVERRIDES, 4), m.r32(MON_OVERRIDES + 4))]
        self.stock_counts = (m.r32(PLR_OVERRIDE_COUNT), m.r32(MON_OVERRIDE_COUNT))
        self.records = {}
        for table, address in ((0, PLR_LINE), (1, MON_LINE), (2, OBJ_LINE)):
            m.hook(address, self.line_hook(table), 2)
        m.hook(RESOLVE, self.resolve, 6)
        m.hook(TRANSFORM, self.transform, 6)
        m.hook(ANIM_MODE, self.anim_mode, 1)
        m.hook(MON_FLAG, self.mon_flag, 0)
        m.hook(ERROR_IP, lambda current: 0x1234, 0)
        m.hook(ERROR_LINE, self.error_line, 0)
        m.hook(FATAL_EXIT, self.fatal, 0)
        if hook_build_cof:
            m.hook(BUILD_COF, self.build_cof, 9)

    def line_hook(self, table):
        """A token-line getter: log (table, index, kind), return a record with the token at +0x20."""
        def hook(current):
            """Hand back the synthetic record for this index and kind."""
            index, kind = current.arg(0), current.arg(1)
            self.log.append(("line", table, index, kind))
            key = (table, index, kind)
            if key not in self.records:
                self.records[key] = current.alloc(0x40)
                current.w32(self.records[key] + 0x20, token_of(table, index, kind))
            return self.records[key]
        return hook

    def resolve(self, current):
        """Weapon-class resolver: log, write the index through the cell, return the token."""
        args = tuple(current.arg(i) for i in range(6))
        self.log.append(("resolve",) + args)
        token, index = resolve_of(args[1], args[2], args[3])
        current.w32(args[5], index)
        return token

    def transform(self, current):
        """State transform: log what the out-parameters hold, then replace them if the case says so."""
        unit, ptype, pcls, pmode, name, line = (current.arg(i) for i in range(6))
        self.log.append(("transform", unit, current.r32(ptype), current.r32(pcls), current.r32(pmode),
                         current.cstr(name), line))
        if self.script.get("transform"):
            for pointer, value in zip((ptype, pcls, pmode), self.script["transform"]):
                current.w32(pointer, value)
        return 0

    def anim_mode(self, current):
        """Unit mode getter."""
        self.log.append(("anim", current.arg(0)))
        return self.script["anim"]

    def mon_flag(self, current):
        """Monster flag test (fastcall: class in ECX, bit in EDX)."""
        cls, bit = current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)
        self.log.append(("flag", cls, bit))
        return self.script["flag"]

    def error_line(self, current):
        """Record the line the fatal report names (cdecl)."""
        self.fatal_line = current.arg(2)
        self.log.append(("error", current.arg(2)))
        return 0

    def fatal(self, current):
        """Stop at the fatal exit."""
        raise Fatal(self.fatal_line)

    def build_cof(self, current):
        """FUN_0064f5b0 replaced by a logger, when the wrapper is the claim."""
        self.log.append(("build",) + tuple(current.arg(i) for i in range(9)))
        return 0

    def run(self, address, *args):
        """Call a stock function; returns (eax, esp delta from a clean stdcall pop of len(args))."""
        sp0 = emu.STACK + emu.STACK_SIZE - 0x1000 - 4 * (len(args) + 1)
        self.log.clear()
        self.fatal_line = None
        value = self.m.call(address, *args, limit=INSTRUCTION_LIMIT)
        require(self.m.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "did not return")
        popped = self.m.mu.reg_read(UC_X86_REG_ESP) - sp0 - 4
        return value, popped, sp0

    def set_overrides(self, player, monster):
        """Write the two override tables (player up to 2 entries, monster up to 1); None keeps game.exe's."""
        m = self.m
        if player is not None:
            for i, (token, mode) in enumerate(player):
                m.mu.mem_write(PLR_OVERRIDES + 8 * i, token)
                m.w32(PLR_OVERRIDES + 8 * i + 4, mode)
            m.w32(PLR_OVERRIDE_COUNT, len(player))
        if monster is not None:
            for i, (token, mode) in enumerate(monster):
                m.mu.mem_write(MON_OVERRIDES + 8 * i, token)
                m.w32(MON_OVERRIDES + 8 * i + 4, mode)
            m.w32(MON_OVERRIDE_COUNT, len(monster))

    def override_lists(self):
        """The override tables as game.exe currently holds them (read back from memory)."""
        m = self.m
        player = [(m.read(PLR_OVERRIDES + 8 * i, 4), m.r32(PLR_OVERRIDES + 8 * i + 4))
                  for i in range(m.r32(PLR_OVERRIDE_COUNT))]
        monster = [(m.read(MON_OVERRIDES + 8 * i, 4), m.r32(MON_OVERRIDES + 8 * i + 4))
                   for i in range(m.r32(MON_OVERRIDE_COUNT))]
        return player, monster

    def make_unit(self, utype, cls, flags):
        """A unit with type, class and the flags dword."""
        unit = self.m.alloc(0x100)
        self.m.w32(unit, utype)
        self.m.w32(unit + 4, cls)
        self.m.w32(unit + 0xC8, flags)
        return unit


def arg_slot(sp0, index):
    """Address of stack argument index for a call whose entry esp was sp0."""
    return sp0 + 4 + 4 * index


def model_build_cof(world, arguments, sp0):
    """Hand-written model of FUN_0064f5b0: (log, out bytes or None, wclass cell value, path or None)."""
    unit, cls, mode, ctype, inv, out, pwc, full, special = arguments
    log, wclass_cell = [], world.script["wclass_cell"]
    signed_cls = struct.unpack("<i", struct.pack("<I", cls))[0]
    player_overrides, monster_overrides = world.override_lists()
    if ctype == 0:
        log.append(("line", 0, cls, 0))
        log.append(("line", 0, mode, 1))
        type_token, mode_token = token_of(0, cls, 0), token_of(0, mode, 1)
        if mut("player-mode-first"):
            type_token, mode_token = mode_token, type_token
        if special:
            if mode in (0, 0x11):
                wclass_cell = HTH
            else:
                log.append(("resolve", unit, 0, cls, mode, inv, arg_slot(sp0, 6)))
                wclass_cell = resolve_of(0, cls, mode)[0]
        for token, entry_mode in (player_overrides[::-1] if mut("override-first-wins") else player_overrides):
            if mode == entry_mode:
                mode_token = word(token)
        if mode == 0xB and wclass_cell not in SIX and not mut("no-mode-b-quirk"):
            wclass_cell = HTH
    elif ctype == 1:
        if signed_cls < 0 or signed_cls >= MONSTERS:
            return log, False, wclass_cell, None
        type_token = struct.unpack("<I", world.m.read(world.monsters + 0x1A8 * cls + 0x10, 4))[0]
        log.append(("line", 1, mode, 1))
        mode_token = token_of(1, mode, 1)
        resolve = True
        if mode in (0, 0xC):
            log.append(("flag", cls, 0x10))
            resolve = bool(world.script["flag"]) != mut("mon-flag-inverted")
            if not resolve:
                wclass_cell = HTH
        if resolve:
            log.append(("resolve", unit, 1, cls, mode, inv, arg_slot(sp0, 4)))
            wclass_cell = resolve_of(1, cls, mode)[0]
        for token, entry_mode in (monster_overrides[::-1] if mut("override-first-wins") else monster_overrides):
            if mode == entry_mode:
                mode_token = word(token)
    elif ctype == 2:
        table = 1 if mut("obj-as-monster") else 2
        log.append(("line", table, cls, 0))
        log.append(("line", table, mode, 1))
        type_token, mode_token = token_of(table, cls, 0), token_of(table, mode, 1)
        log.append(("resolve", unit, 2, cls, mode, inv, arg_slot(sp0, 4)))
        wclass_cell = resolve_of(2, cls, mode)[0]
    else:
        return log, False, wclass_cell, None
    names = (trimmed(type_token), trimmed(mode_token), trimmed(wclass_cell))
    if mut("short-keeps-blank"):
        names = (names[0], names[1], names[2] + "x")
    if full:
        log.append(("sprintf", FULL, (DIRS[ctype], names[0], names[0], names[1], names[2])))
        path = FULL % (DIRS[ctype], names[0], names[0], names[1], names[2])
    else:
        log.append(("sprintf", SHORT, names))
        path = SHORT % names
    return log, True, wclass_cell, path


def check_build_cof(world, case):
    """Run FUN_0064f5b0 for one case and compare with the model."""
    m = world.m
    out, cell = m.alloc(OUT_SIZE), m.alloc(8)
    m.mu.mem_write(out, b"\xaa" * OUT_SIZE)
    m.w32(cell, case["cell"])
    world.script = {"flag": case.get("flag", 0), "wclass_cell": case["cell"]}
    world.set_overrides(case.get("player"), case.get("monster"))
    arguments = (case["unit"], case["cls"], case["mode"], case["type"], case["inv"], out, cell,
                 case["full"], case["special"])
    value, popped, sp0 = world.run(BUILD_COF, *arguments)
    log, wrote, expected_cell, path = model_build_cof(world, arguments, sp0)
    where = f"FUN_0064f5b0 {case}"
    require(popped == 0x24, f"{where}: ret popped {popped:#x}")
    require(world.log == log, f"{where}: call log {world.log} != {log}")
    expected_out = (path.encode("latin-1") + b"\0").ljust(OUT_SIZE, b"\xaa") if wrote else b"\xaa" * OUT_SIZE
    require(m.read(out, OUT_SIZE) == expected_out, f"{where}: out {m.read(out, 32)!r} != {expected_out[:32]!r}")
    require(m.r32(cell) == expected_cell, f"{where}: wclass cell {m.r32(cell):#x} != {expected_cell:#x}")
    return path


def build_cof_cases(rng):
    """Edge cases, then seeded random ones, for FUN_0064f5b0."""
    unit = 0x5000
    base = {"unit": unit, "cls": 3, "mode": 5, "type": 0, "inv": 0x6000, "full": 0, "special": 0, "cell": HTH}
    cases = []
    for ctype in (0, 1, 2, 3, 9):
        for mode in (0, 1, 0xB, 0xC, 0x11, 0x12, 0x13):
            for full in (0, 1):
                for special in (0, 1):
                    for cell in (HTH, word(b"2hs "), word(b"1js "), word(b"1ht ")):
                        for flag in (0, 1):
                            cases.append(dict(base, type=ctype, mode=mode, full=full, special=special, cell=cell,
                                              flag=flag))
    for cls in (0xFFFFFFFF, 0x80000000, MONSTERS, MONSTERS - 1, 0):
        for mode in (0, 5):
            cases.append(dict(base, type=1, cls=cls, mode=mode))
    for _ in range(600):
        cases.append(dict(base, type=rng.choice((0, 1, 2)), cls=rng.randrange(0, MONSTERS) if rng.random() < .6
                          else rng.randrange(0, 25), mode=rng.randrange(0, 0x15), full=rng.randrange(2),
                          special=rng.randrange(2), cell=word(rng.choice(TOKENS)), flag=rng.randrange(2),
                          inv=rng.randrange(1, 1 << 20), unit=rng.randrange(1, 1 << 20)))
    for case in cases:
        if rng.random() < .5:
            case["player"] = [(rng.choice(TOKENS), rng.choice((case["mode"], rng.randrange(0x15))))
                              for _ in range(rng.randrange(0, 3))]
            case["monster"] = [(rng.choice(TOKENS), rng.choice((case["mode"], rng.randrange(0x15))))
                               for _ in range(rng.randrange(0, 2))]
    return cases


def check_stock_tables(world):
    """The override tables game.exe ships with: the 'gh' (get-hit) token for player modes 0x12, 0x13 and monster 0xd."""
    require(world.stock_counts == (2, 1), f"stock override counts {world.stock_counts}")
    require(world.stock_player == [(b"gh  ", 0x12), (b"gh  ", 0x13)], f"stock player overrides {world.stock_player}")
    require(world.stock_monster == [(b"gh  ", 0xD)], f"stock monster overrides {world.stock_monster}")


def check_weapon_class(world, case):
    """Run FUN_0064f380 for one case and compare with the model."""
    m = world.m
    cell = m.alloc(8)
    unit = world.make_unit(case["utype"], case["cls"], case["flags"]) if case["unit"] else 0
    world.script = {"anim": case["anim"], "transform": case["transform"]}
    arguments = (unit, case["inv"], cell, case["mode"], case["ignore"])
    value, popped, sp0 = world.run(WEAPON_CLASS, *arguments)
    where = f"FUN_0064f380 {case}"
    require(popped == 0x14, f"{where}: ret popped {popped:#x}")
    expected = []
    if not unit:
        expected_value, expected_cell = HTH, 0
    else:
        utype, cls, mode = case["utype"], case["cls"], case["mode"]
        if mode == 0xFFFFFFFF:
            expected.append(("anim", unit))
            mode = case["anim"]
        original = mode
        if case["flags"] >> 3 & 1:
            expected.append(("transform", unit, utype, cls, mode, FILE_NAME, 0x236))
            if case["transform"]:
                utype, cls, mode = case["transform"]
            if (not case["ignore"]) != mut("keep-transformed-mode"):
                mode = original
        expected.append(("resolve", unit, utype, cls, mode, case["inv"], cell))
        expected_value, expected_cell = resolve_of(utype, cls, mode)[0], resolve_of(utype, cls, mode)[1]
    require(world.log == expected, f"{where}: call log {world.log} != {expected}")
    require(value == expected_value, f"{where}: returned {value:#x} != {expected_value:#x}")
    require(m.r32(cell) == expected_cell, f"{where}: index cell {m.r32(cell)} != {expected_cell}")


def weapon_class_cases(rng):
    """Edge cases, then seeded random ones, for FUN_0064f380."""
    cases = []
    base = {"unit": True, "utype": 0, "cls": 3, "flags": 0, "mode": 5, "anim": 9, "inv": 0x7000,
            "ignore": 0, "transform": None}
    for flags in (0, 8, 0x10, 0x18, 0xFFFFFFFF):
        for mode in (0xFFFFFFFF, 5, 0):
            for ignore in (0, 1, 7):
                for transform in (None, (1, 40, 13)):
                    cases.append(dict(base, flags=flags, mode=mode, ignore=ignore, transform=transform))
    cases.append(dict(base, unit=False))
    cases.append(dict(base, unit=False, flags=8, mode=0xFFFFFFFF))
    for _ in range(300):
        cases.append(dict(base, utype=rng.randrange(3), cls=rng.randrange(30), flags=rng.choice((0, 8, 0x28, 0x20)),
                          mode=rng.choice((0xFFFFFFFF, rng.randrange(0x15))), anim=rng.randrange(0x15),
                          ignore=rng.randrange(3), inv=rng.randrange(1 << 20),
                          transform=rng.choice((None, (rng.randrange(3), 40 + rng.randrange(9), 15 + rng.randrange(5))))))
    return cases


def check_cof_with_transform(world, case):
    """Run FUN_0064fa00 for one case and compare with the model."""
    m = world.m
    out, cell = m.alloc(OUT_SIZE), m.alloc(8)
    unit = world.make_unit(case["utype"], case["cls"], 8) if case["unit"] else 0
    world.script = {"anim": case["anim"], "transform": case["transform"]}
    arguments = (unit, out, cell, case["full"], case["special"], case["inv"], case["mode"])
    where = f"FUN_0064fa00 {case}"
    if not unit:
        try:
            world.run(COF_WITH_TRANSFORM, *arguments)
        except Fatal as stop:
            line = 0x107 if mut("fatal-line") else 0x106
            require(stop.args[0] == line and world.log == [("error", line)],
                    f"{where}: fatal line {stop.args[0]}, log {world.log}")
            return
        raise Mismatch(f"{where}: null unit did not reach the fatal exit")
    value, popped, sp0 = world.run(COF_WITH_TRANSFORM, *arguments)
    require(popped == 0x1C, f"{where}: ret popped {popped:#x}")
    utype, cls, mode = case["utype"], case["cls"], case["anim"]
    expected = [("anim", unit), ("transform", unit, utype, cls, mode, FILE_NAME, 0x10C)]
    if case["transform"]:
        utype, cls, mode = case["transform"]
    chosen = mode if case["mode"] == 0xFFFFFFFF else case["mode"]
    if mut("swap-class-type"):
        utype, cls = cls, utype
    expected.append(("build", unit, cls, chosen, utype, case["inv"], out, cell, case["full"], case["special"]))
    require(world.log == expected, f"{where}: call log {world.log} != {expected}")


def cof_with_transform_cases(rng):
    """Edge cases, then seeded random ones, for FUN_0064fa00."""
    base = {"unit": True, "utype": 1, "cls": 21, "anim": 4, "mode": 0xFFFFFFFF, "inv": 0x7100, "full": 1,
            "special": 1, "transform": None}
    cases = [dict(base, unit=False), dict(base, unit=False, mode=3)]
    for transform in (None, (0, 33, 12)):
        for mode in (0xFFFFFFFF, 0, 6):
            cases.append(dict(base, mode=mode, transform=transform))
    for _ in range(200):
        cases.append(dict(base, utype=rng.randrange(3), cls=rng.randrange(30), anim=rng.randrange(0x15),
                          mode=rng.choice((0xFFFFFFFF, rng.randrange(0x15))), full=rng.randrange(2),
                          special=rng.randrange(2), inv=rng.randrange(1 << 20),
                          transform=rng.choice((None, (rng.randrange(3), 40 + rng.randrange(9), 15 + rng.randrange(5))))))
    return cases


def run_all(exe):
    """Run all three checks; return (counts, example paths)."""
    rng = random.Random(0x0C0F)
    build = World(exe)
    check_stock_tables(build)
    build_cases = build_cof_cases(rng)
    paths = [check_build_cof(build, case) for case in build_cases]
    # the stock override tables, untouched by set_overrides, with every mode
    stock = World(exe)
    stock_cases = [dict(type=t, cls=1, mode=mode, unit=1, inv=2, full=0, special=0, cell=HTH)
                   for t in (0, 1) for mode in range(0x15)]
    stock_paths = [check_build_cof(stock, case) for case in stock_cases]
    weapon = World(exe)
    weapon_cases = weapon_class_cases(rng)
    for case in weapon_cases:
        check_weapon_class(weapon, case)
    wrapper = World(exe, hook_build_cof=True)
    wrapper_cases = cof_with_transform_cases(rng)
    for case in wrapper_cases:
        check_cof_with_transform(wrapper, case)
    require("gh" in {p[-2:] for p in stock_paths if p}, "no 'gh' override observed with the stock tables")
    return (len(build_cases) + len(stock_cases), len(weapon_cases), len(wrapper_cases)), paths


def main():
    """Verify the three functions, or with --mutation-check prove each model defect is caught."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mutation-check", action="store_true", help="require every deliberate model defect to fail")
    options = parser.parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        sys.exit("requires stock game.exe 1.14d (1.14.3.71)")
    if options.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run_all(str(exe))
            except Mismatch:
                print(f"caught: {name}")
                continue
            sys.exit(f"NOT caught: {name}")
        print(f"ok: {len(MUTATIONS)} deliberate model defects all caught")
        return
    counts, paths = run_all(str(exe))
    print(f"ok: FUN_0064f5b0 {counts[0]} cases, FUN_0064f380 {counts[1]}, FUN_0064fa00 {counts[2]}; "
          f"e.g. {sorted(set(p for p in paths if p))[0]}")


if __name__ == "__main__":
    main()
