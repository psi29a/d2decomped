# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the eight frontier entries of game.exe's client UI start.

Run: .venv/bin/python ui_start_frontier.py [--exe PATH] [--mutation-check]

FUN_00456970 (the client UI reset at a game's start) calls these eight
untouched functions; each one runs here in stock game.exe 1.14d with its
loaders and registry calls hooked and logged, against an independent model:

- FUN_0047f0c0: the mini-panel art. Game type 0 (single player, 0x7a0610)
  takes ``Panel\\minipanel_s`` and flag 0x7bc978 = 1; any other type takes
  ``Panel\\minipanel``, flag 0. Both load ``Panel\\minipanelbtn``, then clear
  7 (flag 1) or 8 button-down dwords at 0x7bc8f8.
- FUN_0049d440: clears 0x9f bytes at 0x7bf1b0, then 0x7bf1f6 = registry
  ``Diablo II\\Text Display Beta``, or 1 when the read fails.
- FUN_0049c6a0: 0x7bf081 = FUN_00660f30(0), byte 0x7bf085 = 0.
- FUN_004b7b40: 0x7c0e58 = 0.
- FUN_00489220: loads buyselltabs and miniconvert when their slots are
  empty, invchar6tab only when its slot is empty, d2exp.mpq was found
  (FUN_00408f20) and the game is an expansion one (0x7a04f4).
- FUN_004a3410: writes registry ``Diablo II\\GAMEOVER`` = 0, then resets the
  quest log's state (the exact bytes are modelled below).
- FUN_004a5190: the controls screen's layout by expansion (FUN_004a43e0 runs
  for real: rows table, row count, last scroll and thumb rows 5 or 6), the
  ``UI\\Menu\\TextSlid`` slider, FUN_004a5060, then scroll and selection 0.
- FUN_0046ac70: loads the key bindings (FUN_0046aae0), 0x7a7418 = 1, and
  the first time (0x7a7428 was 0) installs the four key-message handlers at
  0x712944 on window [0x7c8cbc] through FUN_00451db0.
"""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

if not __debug__:
    sys.exit("ui_start_frontier.py requires assertions; do not use python -O")

# gaps-runs: 0047f0c0 0049d440 0049c6a0 004b7b40 00489220 004a3410 004a5190 0046ac70

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MINIPANEL_LOAD = 0x47F0C0
TEXT_INIT = 0x49D440
WAYPOINT_INIT = 0x49C6A0
DIALOGS_INIT = 0x4B7B40
STORE_CELS_LOAD = 0x489220
QUEST_LOG_INIT = 0x4A3410
CONTROLS_INIT = 0x4A5190
KEYS_START = 0x46AC70

UI_DC6_LOAD = 0x4520C0
LOAD_DC6 = 0x4788B0
REGISTRY_DWORD_READ = 0x414F10
REGISTRY_DWORD_WRITE = 0x4150E0
WAYPOINT_NEW = 0x660F30
D2EXP_FOUND = 0x408F20
FORMAT_PATH = 0x413A40
SPRINTF = 0x6823FD
CONTROLS_BUTTONS = 0x4A5060
KEYS_LOAD = 0x46AAE0
HANDLERS_ADD = 0x451DB0

GAME_TYPE = 0x7A0610
EXPANSION = 0x7A04F4
MAIN_WINDOW = 0x7C8CBC
CALL_LIMIT = 200000
FILL = 0xA5
mutation = None


class Mismatch(Exception):
    """The stock result disagreed with the independent model."""


def require(condition, message):
    """Raise a comparison failure with context."""
    if not condition:
        raise Mismatch(message)


def fill(machine, start, length, byte=FILL):
    """Fill a span of game.exe's data with a known byte."""
    machine.mu.mem_write(start, bytes([byte]) * length)


def pattern(length, seed):
    """Nonzero, non-0xff bytes so a write of 0 or -1 always shows."""
    return bytes(((index * 7 + seed) % 0xFD) + 1 for index in range(length))


class Log:
    """The hooked callees' calls, in order, and the next return value."""

    def __init__(self, machine):
        self.machine = machine
        self.calls = []
        self.next_value = 0x51000000
        self.d2exp = 0
        self.registry = None

    def value(self):
        self.next_value += 0x10
        return self.next_value

    def install(self):
        machine = self.machine

        def ui_dc6(current):
            self.calls.append(("ui_dc6", current.cstr(current.mu.reg_read(UC_X86_REG_ECX))))
            return self.value()

        def dc6(current):
            self.calls.append(("dc6", current.cstr(current.mu.reg_read(UC_X86_REG_ECX)),
                               current.mu.reg_read(UC_X86_REG_EDX)))
            return self.value()

        def registry_read(current):
            self.calls.append(("reg_read", current.cstr(current.arg(0)), current.cstr(current.arg(1)), current.arg(2)))
            if self.registry is None:
                return 0
            current.w32(current.arg(3), self.registry)
            return 1

        def registry_write(current):
            self.calls.append(("reg_write", current.cstr(current.arg(0)), current.cstr(current.arg(1)),
                               current.arg(2), current.arg(3)))
            return 1

        def waypoint_new(current):
            self.calls.append(("waypoint_new", current.arg(0)))
            return self.value()

        def d2exp(current):
            self.calls.append(("d2exp",))
            return self.d2exp

        def format_path(current):
            # cdecl (buffer, size, format, argument): the stock CRT formatter
            # loops in the emulator's locale code, so its one %s is done here.
            text = current.cstr(current.arg(2)).replace("%s", current.cstr(current.arg(3)), 1)
            data = text.encode("latin-1")[:current.arg(1) - 1] + b"\0"
            current.mu.mem_write(current.arg(0), data)
            return len(data) - 1

        def sprintf_path(current):
            # cdecl (buffer, format, argument), the CRT sprintf, as format_path.
            text = current.cstr(current.arg(1)).replace("%s", current.cstr(current.arg(2)), 1)
            current.mu.mem_write(current.arg(0), text.encode("latin-1") + b"\0")
            return len(text)

        def buttons(current):
            self.calls.append(("controls_buttons",))
            return 0

        def keys_load(current):
            self.calls.append(("keys_load",))
            return 0

        def handlers(current):
            self.calls.append(("handlers", current.mu.reg_read(UC_X86_REG_ECX),
                               current.mu.reg_read(UC_X86_REG_EDX), current.arg(0)))
            return 0

        machine.hook(UI_DC6_LOAD, ui_dc6, 0)
        machine.hook(LOAD_DC6, dc6, 0)
        machine.hook(REGISTRY_DWORD_READ, registry_read, 4)
        machine.hook(REGISTRY_DWORD_WRITE, registry_write, 4)
        machine.hook(WAYPOINT_NEW, waypoint_new, 1)
        machine.hook(D2EXP_FOUND, d2exp, 0)
        machine.hook(FORMAT_PATH, format_path, 0)
        machine.hook(SPRINTF, sprintf_path, 0)
        machine.hook(CONTROLS_BUTTONS, buttons, 0)
        machine.hook(KEYS_LOAD, keys_load, 0)
        machine.hook(HANDLERS_ADD, handlers, 1)

    def run(self, address):
        self.calls.clear()
        start = self.next_value
        self.machine.call(address, limit=CALL_LIMIT)
        eip = self.machine.mu.reg_read(UC_X86_REG_EIP)
        require(eip == emu.RET_MAGIC, f"{address:#x} stopped at {eip:#x} without returning")
        return start


def check_minipanel(machine, log):
    """FUN_0047f0c0 over every game type the front end sets, and the extremes."""
    types = [0, 1, 2, 3, 6, 7, 8, 9, 0x80000000, 0xFFFFFFFF]
    for game_type in types:
        machine.w32(GAME_TYPE, game_type)
        fill(machine, 0x7BC8F8, 9 * 4)
        machine.w32(0x7BC978, 0x5A5A5A5A)
        start = log.run(MINIPANEL_LOAD)
        single = game_type == 0
        if mutation == "minipanel_type_inverted":
            single = not single
        panel = "Panel\\minipanel_s" if single else "Panel\\minipanel"
        want_calls = [("ui_dc6", panel), ("ui_dc6", "Panel\\minipanelbtn")]
        require(log.calls == want_calls, f"minipanel type {game_type:#x}: game {log.calls}, model {want_calls}")
        require(machine.r32(0x7BC960) == start + 0x10, f"minipanel type {game_type:#x}: panel slot")
        require(machine.r32(0x7BC964) == start + 0x20, f"minipanel type {game_type:#x}: button slot")
        require(machine.r32(0x7BC978) == int(single), f"minipanel type {game_type:#x}: flag {machine.r32(0x7BC978):#x}")
        cleared = 7 if single and mutation != "minipanel_clear_8" else 8
        got = [machine.r32(0x7BC8F8 + 4 * index) for index in range(9)]
        want = [0] * cleared + [0xA5A5A5A5] * (9 - cleared)
        require(got == want, f"minipanel type {game_type:#x}: buttons {got}, model {want}")
    return len(types)


def check_text(machine, log):
    """FUN_0049d440 with a failed read and with stored values."""
    stored = [None, 0, 1, 2, 0x7FFFFFFF, 0xFFFFFFFF]
    for value in stored:
        fill(machine, 0x7BF1A0, 0xBF)
        log.registry = value
        log.run(TEXT_INIT)
        want_calls = [("reg_read", "Diablo II", "Text Display Beta", 0)]
        require(log.calls == want_calls, f"text {value}: game {log.calls}")
        default = 0 if mutation == "text_default_0" else 1
        want = bytearray([FILL] * 0xBF)
        want[0x10:0x10 + 0x9F] = bytes(0x9F)
        want[0x10 + 0x46:0x10 + 0x4A] = struct.pack("<I", default if value is None else value)
        got = machine.read(0x7BF1A0, 0xBF)
        require(got == bytes(want), f"text {value}: state {got.hex()}")
    log.registry = None
    return len(stored)


def check_small(machine, log):
    """FUN_0049c6a0 and FUN_004b7b40: one store each, no neighbour touched."""
    fill(machine, 0x7BF07D, 0x10)
    start = log.run(WAYPOINT_INIT)
    require(log.calls == [("waypoint_new", 0)], f"waypoint: game {log.calls}")
    want = bytearray([FILL] * 0x10)
    want[4:8] = struct.pack("<I", start + 0x10)
    want[8] = 0
    require(machine.read(0x7BF07D, 0x10) == bytes(want), f"waypoint: state {machine.read(0x7BF07D, 0x10).hex()}")
    fill(machine, 0x7C0E50, 0x10)
    log.run(DIALOGS_INIT)
    want = bytearray([FILL] * 0x10)
    want[8:12] = bytes(4)
    require(log.calls == [] and machine.read(0x7C0E50, 0x10) == bytes(want), "dialogs: state")
    return 2


def check_store_cels(machine, log):
    """FUN_00489220 over every slot state, d2exp.mpq found or not, and the expansion flag."""
    slots = (0x7BCC24, 0x7BCC28, 0x7BCC38)
    names = ("DATA\\GLOBAL\\ui\\panel\\buyselltabs", "DATA\\GLOBAL\\ui\\panel\\miniconvert",
             "DATA\\GLOBAL\\ui\\panel\\invchar6tab")
    cases = 0
    for loaded in range(8):
        for found in (0, 1):
            for expansion in (0, 1, 2, 0xFFFFFFFF):
                for index, slot in enumerate(slots):
                    machine.w32(slot, 0x7700 + index if loaded >> index & 1 else 0)
                log.d2exp = found
                machine.w32(EXPANSION, expansion)
                start = log.run(STORE_CELS_LOAD)
                want_calls, want_slots, value = [], [], start
                for index in range(3):
                    if loaded >> index & 1:
                        want_slots.append(0x7700 + index)
                        continue
                    if index == 2:
                        want_calls.append(("d2exp",))
                        if not found or (not expansion and mutation != "tab6_ignores_expansion"):
                            want_slots.append(0)
                            continue
                    want_calls.append(("dc6", names[index], 0))
                    value += 0x10
                    want_slots.append(value)
                got_slots = [machine.r32(slot) for slot in slots]
                label = f"store cels {loaded:03b} d2exp {found} expansion {expansion:#x}"
                require(log.calls == want_calls, f"{label}: game {log.calls}, model {want_calls}")
                require(got_slots == want_slots, f"{label}: slots {got_slots}, model {want_slots}")
                cases += 1
    log.d2exp = 0
    return cases


def quest_log_model():
    """The quest-log bytes FUN_004a3410 leaves, over 0x7bf290..0x7bf3b0 and 0x7c0250..0x7c0268."""
    low = bytearray(pattern(0x120, 3))
    high = bytearray(pattern(0x18, 11))

    def put(address, data):
        if 0x7BF290 <= address < 0x7BF3B0:
            low[address - 0x7BF290:address - 0x7BF290 + len(data)] = data
        else:
            high[address - 0x7C0250:address - 0x7C0250 + len(data)] = data

    for address in (0x7BF298, 0x7BF2D5, 0x7BF2D9, 0x7BF2D1, 0x7BF2DD, 0x7BF351, 0x7BF34D):
        put(address, bytes(4))
    put(0x7BF2E1, bytes(0x6C))
    for address in (0x7BF2A4, 0x7BF2AC, 0x7C0259):
        put(address, bytes(4))
    count = 0x28 if mutation == "quest_loop_short" else 0x29
    put(0x7BF356, bytes(count))
    put(0x7BF380, bytes(count))
    for address in (0x7BF2B1, 0x7BF2B2, 0x7BF2B0, 0x7BF2B4, 0x7C025D, 0x7C025E, 0x7C025F, 0x7C0260, 0x7BF2B3):
        put(address, b"\0")
    for address in (0x7BF2B5, 0x7BF2BD, 0x7BF2C1, 0x7BF2C5, 0x7BF2C9, 0x7BF2CD):
        put(address, b"\xff" * 4)
    return bytes(low), bytes(high)


def check_quest_log(machine, log):
    """FUN_004a3410: the registry write and every byte it resets."""
    machine.mu.mem_write(0x7BF290, pattern(0x120, 3))
    machine.mu.mem_write(0x7C0250, pattern(0x18, 11))
    log.run(QUEST_LOG_INIT)
    written = 1 if mutation == "gameover_one" else 0
    want_calls = [("reg_write", "Diablo II", "GAMEOVER", 0, written)]
    require(log.calls == want_calls, f"quest log: game {log.calls}, model {want_calls}")
    low, high = quest_log_model()
    require(machine.read(0x7BF290, 0x120) == low, "quest log: 0x7bf290.. state")
    require(machine.read(0x7C0250, 0x18) == high, "quest log: 0x7c0250.. state")
    return 1


def check_controls(machine, log):
    """FUN_004a5190 (and FUN_004a43e0 inside it) for classic and expansion games."""
    flags = [0, 1, 2, 0xFFFFFFFF]
    for expansion in flags:
        machine.w32(EXPANSION, expansion)
        for address in (0x724264, 0x7C0274, 0x7C0278, 0x7C027C, 0x7C02AC, 0x7C02B0, 0x7C02BC):
            machine.w32(address, 0x5A5A5A5A)
        start = log.run(CONTROLS_INIT)
        want_calls = [("dc6", "DATA\\GLOBAL\\UI\\Menu\\TextSlid", 0), ("controls_buttons",)]
        require(log.calls == want_calls, f"controls {expansion:#x}: game {log.calls}, model {want_calls}")
        lod = expansion != 0
        thumb = (6 if lod else 5) if mutation == "thumb_rows_swapped" else (5 if lod else 6)
        want = {0x724264: 0x724468 if lod else 0x724268, 0x7C0274: 0x3E if lod else 0x33,
                0x7C027C: 0x2F if lod else 0x24, 0x7C0278: thumb,
                0x7C02AC: 0, 0x7C02B0: 0, 0x7C02BC: start + 0x10}
        got = {address: machine.r32(address) for address in want}
        require(got == want, f"controls {expansion:#x}: {got}, model {want}")
    return len(flags)


def check_keys(machine, log):
    """FUN_0046ac70 with the handlers not yet installed and already installed."""
    table = [struct.unpack_from("<III", machine.read(0x712944, 48), 12 * index) for index in range(4)]
    want_table = [(0, 0x100, 0x46A840), (0, 0x104, 0x46A840), (0, 0x101, 0x46A940), (0, 0x105, 0x46A940)]
    require(table == want_table, f"key handler table {table}")
    states = [0, 1, 2, 0xFFFFFFFF]
    for installed in states:
        machine.w32(0x7A7418, 0x77777777)
        machine.w32(0x7A7428, installed)
        machine.w32(MAIN_WINDOW, 0x00C0FFEE)
        log.run(KEYS_START)
        want_calls = [("keys_load",)]
        if installed == 0 or mutation == "handlers_every_time":
            want_calls.append(("handlers", 0x00C0FFEE, 0x712944, 4))
        require(log.calls == want_calls, f"keys {installed:#x}: game {log.calls}, model {want_calls}")
        require(machine.r32(0x7A7418) == 1, f"keys {installed:#x}: 0x7a7418 {machine.r32(0x7A7418):#x}")
        want_installed = 1 if installed == 0 else installed
        require(machine.r32(0x7A7428) == want_installed, f"keys {installed:#x}: 0x7a7428 {machine.r32(0x7A7428):#x}")
    return len(states)


def check(exe):
    """Run all eight entries; returns each group's case count."""
    machine = emu.Emu(exe=exe)
    log = Log(machine)
    log.install()
    return {
        "mini-panel": check_minipanel(machine, log),
        "text": check_text(machine, log),
        "waypoint/dialogs": check_small(machine, log),
        "store cels": check_store_cels(machine, log),
        "quest log": check_quest_log(machine, log),
        "controls": check_controls(machine, log),
        "keys": check_keys(machine, log),
    }


MUTATIONS = ("minipanel_type_inverted", "minipanel_clear_8", "text_default_0", "tab6_ignores_expansion",
             "gameover_one", "quest_loop_short", "thumb_rows_swapped", "handlers_every_time")


def main():
    """Parse the executable, run the positive check and optional negative controls."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe")
    parser.add_argument("--mutation-check", action="store_true")
    options = parser.parse_args()
    exe = options.exe or str(Path(emu.data_path()) / "bin" / "game.exe")
    require(hashlib.sha256(Path(exe).read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    counts = check(exe)
    print("ok: " + ", ".join(f"{count} {name}" for name, count in counts.items()))
    if options.mutation_check:
        global mutation
        for defect in MUTATIONS:
            mutation = defect
            try:
                check(exe)
            except Mismatch:
                print(f"mutation {defect}: failed as required")
            else:
                raise RuntimeError(f"mutation {defect} was not detected")
        mutation = None


if __name__ == "__main__":
    main()
