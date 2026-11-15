# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the 20 functions at the top of the 2026-11-15 Blocking gap list.

    uv run python blocking_top20.py --exe /path/to/game.exe [--mutation-check]

The checks use synthetic records and hook callees outside each claim.  They
cover FUN_0064a6d0, FUN_005408e0, FUN_0053fc70, FUN_0044ec80,
FUN_004b9c20, FUN_005d92d0, FUN_00469ae0, FUN_0056c600, FUN_0052d240,
FUN_0054ef50, FUN_004ed480, FUN_004eda80, FUN_0042f6e0, FUN_0053e8f0,
FUN_00532340, FUN_00515890, FUN_004e9870, FUN_0057f410, FUN_005e77a0 and
FUN_00452af0.  No MPQ data is read.

``--mutation-check`` changes one expected behavior per function and requires
every changed model to disagree with game.exe.
"""
# gaps-runs: 0064a6d0 005408e0 0053fc70 0044ec80 004b9c20 005d92d0 00469ae0 0056c600 0052d240 0054ef50
# gaps-runs: 004ed480 004eda80 0042f6e0 0053e8f0 00532340 00515890 004e9870 0057f410 005e77a0 00452af0
import argparse
import hashlib
import random
import struct
from pathlib import Path

from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX,
                               UC_X86_REG_EDX, UC_X86_REG_ESI)

import emu
from checked_emu import CheckedEmu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MASK = 0xFFFFFFFF
MUTATIONS = (
    "stream-writes-any-unit", "nearby-forgets-zero", "client-callback-type4",
    "end-flag-overwrites-word", "sound-list-includes-type2", "dual-one-weapon",
    "key-allows-left-mouse", "throw-replaces-tohit", "room-age-ten",
    "wanderer-counts-failed-spawn", "learned-no-cache", "rating-bound-18",
    "connecting-skips-third", "assign-player-short-bound", "save-skips-finish",
    "pan-does-not-mirror", "dual-line-y-minus-six", "remove-keeps-bit",
    "cain-always-default", "player-name-no-relation",
)
mutation = None


def choose(name, correct, changed):
    """Return the intentionally changed expectation for one mutation."""
    return changed if mutation == name else correct


def registers(machine):
    """The volatile/custom registers useful to hook logs."""
    return tuple(machine.mu.reg_read(register) for register in
                 (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESI))


def check_stream_setter(machine, rng):
    """FUN_0064a6d0 writes EDX to missile-data +0x30 only for a type-3 unit with data."""
    cases = 0
    for kind in (0, 1, 2, 3, 4, MASK):
        for has_data in (False, True):
            unit, data = machine.alloc(0x40), machine.alloc(0x40)
            before, value = rng.getrandbits(32), rng.getrandbits(32)
            machine.w32(unit, kind)
            machine.w32(unit + 0x14, data if has_data else 0)
            machine.w32(data + 0x30, before)
            machine.w32(data + 0x34, before ^ MASK)
            machine.call(0x64A6D0, ecx=unit, edx=value, limit=100)
            writes = has_data and (kind == 3 or mutation == "stream-writes-any-unit")
            expected = value if writes else before
            if machine.r32(data + 0x30) != expected or machine.r32(data + 0x34) != (before ^ MASK):
                raise AssertionError(f"FUN_0064a6d0 type {kind} data {has_data}")
            cases += 1
    machine.call(0x64A6D0, ecx=0, edx=rng.getrandbits(32), limit=100)
    return cases + 1


def check_nearby_count(machine, rng):
    """FUN_005408e0 zeroes a local count, forwards ECX/EDX and returns the enumerator's count."""
    seen = []

    def enumerate_party(current):
        callback, count = current.arg(0), current.arg(1)
        seen.append((current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                     callback, current.r32(count)))
        current.w32(count, 7)
        return 0

    machine.hook(0x5405A0, enumerate_party, 2)
    for _ in range(20):
        ecx, edx = rng.getrandbits(32), rng.getrandbits(32)
        seen.clear()
        got = machine.call(0x5408E0, ecx=ecx, edx=edx, limit=200)
        initial = choose("nearby-forgets-zero", 0, 1)
        if got != 7 or seen != [(ecx, edx, 0x5404F0, initial)]:
            raise AssertionError(f"FUN_005408e0 returned {got}, calls {seen}")


def check_client_callback(machine, rng):
    """FUN_0053fc70 broadcasts first, then conditionally refreshes a non-type-4 client's unit."""
    log, unit_result = [], {"value": 0}
    machine.hook(0x53FB90, lambda c: (log.append(("broadcast", c.arg(0), c.arg(1), c.mu.reg_read(UC_X86_REG_EBX))), 0)[1], 2)
    machine.hook(0x537860, lambda c: (log.append(("unit", c.arg(0), c.arg(1))), unit_result["value"])[1], 2)
    machine.hook(0x53FB30, lambda c: (log.append(("refresh", c.arg(0), c.mu.reg_read(UC_X86_REG_ESI))), 0)[1], 1)
    for client_type in (0, 3, 4, 5):
        for found in (0, rng.getrandbits(32) | 1):
            game, client = rng.getrandbits(32), machine.alloc(0x500)
            machine.w32(client + 4, client_type)
            unit_result["value"] = found
            log.clear()
            machine.call(0x53FC70, ecx=game, edx=client, limit=500)
            expected = [("broadcast", game, 1, client), ("unit", client, 1)]
            refresh = found != 0 and (client_type != 4 or mutation == "client-callback-type4")
            if refresh:
                expected.append(("refresh", client, found))
            if log != expected:
                raise AssertionError(f"FUN_0053fc70 type {client_type} found {found:#x}: {log}")


def check_end_flag(machine, rng):
    """FUN_0044ec80 marks classic ending and replaces bits 8..12 of the screen word."""
    screen = machine.alloc(0x300)
    machine.w32(0x7A0438, screen)
    for ending in range(8):
        before = rng.randrange(0x10000)
        machine.mu.mem_write(screen + 0x1EF, struct.pack("<H", before))
        machine.w32(0x7A0604, rng.getrandbits(32))
        machine.w32(0x7A060C, ending)
        machine.call(0x44EC80, limit=100)
        expected_word = ((ending + 1) * 0x400 | (before & 0xE0FF)) & 0xFFFF
        if mutation == "end-flag-overwrites-word":
            expected_word = ((ending + 1) * 0x400) & 0xFFFF
        got_word = struct.unpack("<H", machine.read(screen + 0x1EF, 2))[0]
        if machine.r32(0x7A0604) != 1 or got_word != expected_word:
            raise AssertionError(f"FUN_0044ec80 ending {ending}: {got_word:#x}")


def check_sound_list(machine, rng):
    """FUN_004b9c20 finds an active non-type-2 sound whose id is 0xb76..0x1230."""
    def run(rows):
        head = 0
        for sound_type, sound_id in reversed(rows):
            sound, node = machine.alloc(0x40), machine.alloc(8)
            machine.w32(sound + 0x30, sound_type)
            machine.w32(sound + 4, sound_id)
            machine.w32(node, sound)
            machine.w32(node + 4, head)
            head = node
        machine.w32(0x7C5458, head)
        return machine.call(0x4B9C20, limit=500)

    cases = ((), ((2, 0xB76),), ((1, 0xB75),), ((1, 0xB76),), ((0, 0x1230),),
             ((3, 0x1231),), ((2, 0xB76), (4, 0x1230)), ((1, rng.randrange(0xB76, 0x1231)),))
    for rows in cases:
        correct = int(any(sound_type != 2 and 0xB76 <= sound_id <= 0x1230 for sound_type, sound_id in rows))
        changed = int(any(0xB76 <= sound_id <= 0x1230 for _, sound_id in rows))
        if run(rows) != choose("sound-list-includes-type2", correct, changed):
            raise AssertionError(f"FUN_004b9c20 rows {rows}")


def check_dual_wield(machine):
    """FUN_005d92d0 requires both body slots 6 and 5 and item flag 0x2e on each."""
    slots, flags = {6: 0, 5: 0}, {}
    machine.hook(0x63C050, lambda c: slots[c.arg(1)], 2)
    machine.hook(0x629BB0, lambda c: flags.get(c.arg(0), 0) if c.arg(1) == 0x2E else 0, 2)
    player, inventory, first, second = machine.alloc(0x100), machine.alloc(0x20), machine.alloc(0x20), machine.alloc(0x20)
    for has_inventory in (False, True):
        for have_first in (False, True):
            for have_second in (False, True):
                for first_weapon in (False, True):
                    for second_weapon in (False, True):
                        machine.w32(player + 0x60, inventory if has_inventory else 0)
                        slots[6] = first if have_first else 0
                        slots[5] = second if have_second else 0
                        flags[first], flags[second] = int(first_weapon), int(second_weapon)
                        got = machine.call(0x5D92D0, regs={"eax": player}, limit=500)
                        correct = int(has_inventory and have_first and have_second and first_weapon and second_weapon)
                        changed = int(has_inventory and have_first and first_weapon)
                        if got != choose("dual-one-weapon", correct, changed):
                            raise AssertionError("FUN_005d92d0 dual-wield predicate")


def check_assignable_key(machine):
    """FUN_00469ae0 implements the exact fixed-key exclusions plus ASCII alphanumeric fallback."""
    machine.hook(0x686DA5, lambda c: int(0x41 <= c.arg(0) <= 0x5A or 0x61 <= c.arg(0) <= 0x7A), 0)
    machine.hook(0x686F0F, lambda c: int(0x30 <= c.arg(0) <= 0x39), 0)
    denied = {1, 2, 3, 4, 0x15, 0x17, 0x18, 0x19, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
              0x25, 0x26, 0x27, 0x28, 0x29, 0x2B, 0x5B, 0x5C, 0x5D, 0x90}
    allowed = {8, 9, 0xC, 0xD, 0x10, 0x11, 0x12, 0x13, 0x14, 0x20, 0x21, 0x22,
               0x23, 0x24, 0x2A, 0x2C, 0x2D, 0x2E, 0x2F, *range(0x60, 0x88), 0x91,
               *range(0xBA, 0xC1), *range(0xDB, 0xDF), *range(0x100, 0x105)}
    for key in range(0x200):
        correct = int(key in allowed or (key not in denied and
                      (0x41 <= key <= 0x5A or 0x61 <= key <= 0x7A or 0x30 <= key <= 0x39)))
        changed = 1 if key == 1 else correct
        got = machine.call(0x469AE0, ecx=key, limit=500)
        if got != choose("key-allows-left-mouse", correct, changed):
            raise AssertionError(f"FUN_00469ae0 key {key:#x}: {got}")


def check_throw_mastery(machine, rng):
    """FUN_0056c600 adds throw to-hit to stat 0x13 and sets throw damage percent stat 0x19."""
    log, base = [], {"value": 0}
    machine.hook(0x625480, lambda c: (log.append(("read", c.arg(0), c.arg(1), c.arg(2))), base["value"])[1], 3)
    machine.hook(0x645720, lambda c: (log.append(("mastery", tuple(c.arg(i) for i in range(5)))), 30 + c.arg(3))[1], 5)
    machine.hook(0x627260, lambda c: (log.append(("add", tuple(c.arg(i) for i in range(4)))), 0)[1], 4)
    machine.hook(0x6272B0, lambda c: (log.append(("set", tuple(c.arg(i) for i in range(4)))), 0)[1], 4)
    player, missile, skill = (rng.getrandbits(32) | 0x1000 for _ in range(3))
    for values in ((0, missile, skill), (player, 0, skill), (player, missile, 0)):
        log.clear()
        machine.call(0x56C600, values[2], ecx=values[0], edx=values[1], limit=500)
        if log:
            raise AssertionError(f"FUN_0056c600 null guard called {log}")
    base["value"] = 12
    log.clear()
    machine.call(0x56C600, skill, ecx=player, edx=missile, limit=500)
    correct = [("read", missile, 0x13, 0), ("mastery", (player, skill, 0, 0, 0)),
               ("add", (missile, 0x13, 42, 0)), ("mastery", (player, skill, 0, 1, 0)),
               ("set", (missile, 0x19, 31, 0))]
    changed = correct.copy()
    changed[2] = ("add", (missile, 0x13, 30, 0))
    if log != choose("throw-replaces-tohit", correct, changed):
        raise AssertionError(f"FUN_0056c600 calls {log}")


def check_room_expiry(machine):
    """FUN_0052d240 walks five acts, saves next first, and frees valid idle rooms after age 10."""
    log, ages, removable = [], {}, {}
    heads = {}
    machine.hook(0x61A180, lambda c: heads.get(c.arg(0), 0), 1)
    machine.hook(0x61A790, lambda c: ages[c.arg(0)], 1)
    machine.hook(0x61A3F0, lambda c: removable[c.arg(1)], 2)
    machine.hook(0x5433F0, lambda c: (log.append(("unit", c.mu.reg_read(UC_X86_REG_ECX), c.mu.reg_read(UC_X86_REG_EDX))), 0)[1], 0)
    machine.hook(0x61A910, lambda c: (log.append(("free", c.arg(0), c.arg(1))), 0)[1], 2)
    game = machine.alloc(0x200)
    acts = [machine.alloc(0x20) for _ in range(5)]
    rooms = [machine.alloc(0x100) for _ in range(4)]
    units = [machine.alloc(0x100) for _ in range(3)]
    for index, act in enumerate(acts):
        machine.w32(game + 0xBC + index * 4, act)
        heads[act] = 0
    heads[acts[0]], heads[acts[1]] = rooms[0], rooms[2]
    machine.w32(rooms[0] + 0x7C, rooms[1])
    machine.w32(rooms[1] + 0x7C, 0)
    machine.w32(rooms[2] + 0x7C, rooms[3])
    machine.w32(rooms[3] + 0x7C, 0)
    ages.update({rooms[0]: 10, rooms[1]: 11, rooms[2]: 50, rooms[3]: 12})
    removable.update({rooms[0]: 1, rooms[1]: 1, rooms[2]: 0, rooms[3]: 1})
    machine.w32(rooms[1] + 0x74, units[0])
    machine.w32(units[0] + 0xE8, units[1])
    machine.w32(units[1] + 0xE8, 0)
    machine.w32(rooms[3] + 0x74, units[2])
    machine.w32(units[2] + 0xE8, 0)
    machine.call(0x52D240, game, limit=2000)
    correct = [("unit", game, units[0]), ("unit", game, units[1]), ("free", acts[0], rooms[1]),
               ("unit", game, units[2]), ("free", acts[1], rooms[3])]
    changed = [("free", acts[0], rooms[0]), *correct]
    if log != choose("room-age-ten", correct, changed):
        raise AssertionError(f"FUN_0052d240 calls {log}")


def check_wanderer(machine):
    """FUN_0054ef50 picks the per-act class, tries a spot and spawn, then lists/counts success."""
    log, random_value, spot_ok, spawn_ok = [], {"value": 0}, {"value": 1}, {"value": 1}
    machine.hook(0x45C390, lambda c: (log.append(("random", c.mu.reg_read(UC_X86_REG_ECX), c.mu.reg_read(UC_X86_REG_EDX))), random_value["value"])[1], 0)

    def spot(current):
        log.append(("spot", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                    tuple(current.arg(i) for i in range(5))))
        current.w32(current.arg(2), 101)
        current.w32(current.arg(3), 202)
        return spot_ok["value"]

    machine.hook(0x54DC40, spot, 5)
    machine.hook(0x5B2F20, lambda c: (log.append(("spawn", c.mu.reg_read(UC_X86_REG_ECX), c.mu.reg_read(UC_X86_REG_EDX),
                                                        tuple(c.arg(i) for i in range(6)))), spawn_ok["value"])[1], 6)
    machine.hook(0x5B1990, lambda c: (log.append(("list", c.mu.reg_read(UC_X86_REG_ECX), c.mu.reg_read(UC_X86_REG_EDX),
                                                       c.arg(0), c.arg(1))), 0)[1], 2)
    game, region, counter, act = (machine.alloc(0x400) for _ in range(4))
    for act_index in range(7):
        machine.mu.mem_write(act + 3, bytes([act_index]))
        for spot_value, spawn_value in ((0, 1), (1, 0), (1, 1)):
            spot_ok["value"], spawn_ok["value"] = spot_value, spawn_value
            machine.w32(counter + 0x2C4, 9)
            log.clear()
            machine.call(0x54EF50, counter, regs={"eax": act, "ebx": game, "edi": region}, limit=1000)
            success = act_index < 5 and machine.read(0x731B31 + act_index * 2, 1)[0] != 0 and spot_value and spawn_value
            expected_count = 10 if success else 9
            if mutation == "wanderer-counts-failed-spawn" and act_index < 5 and spot_value:
                expected_count = 10
            if machine.r32(counter + 0x2C4) != expected_count:
                raise AssertionError(f"FUN_0054ef50 act {act_index} spot {spot_value} spawn {spawn_value}")
            if success and log[-1][:1] != ("list",):
                raise AssertionError(f"FUN_0054ef50 success calls {log}")


def check_learned_skill(machine):
    """FUN_004ed480 searches every class skill for points and caches the first yes globally."""
    table, rows, player = machine.alloc(0xC00), machine.alloc(3 * 0x23C), machine.alloc(0x20)
    machine.w32(0x744304, table)
    machine.w32(table + 0xB98, rows)
    machine.w32(table + 0xBA0, 3)
    machine.mu.mem_write(rows + 0xC, b"\x01")
    machine.mu.mem_write(rows + 0x23C + 0xC, b"\x02")
    machine.mu.mem_write(rows + 2 * 0x23C + 0xC, b"\x01")
    calls, levels = [], {0: 0, 1: 0, 2: 0}
    machine.hook(0x451F60, lambda c: 1, 0)
    machine.hook(0x6439B0, lambda c: (calls.append(("skill", c.mu.reg_read(UC_X86_REG_ECX),
                                                          c.mu.reg_read(UC_X86_REG_EDX), c.arg(0))),
                                                 0x9000 + c.mu.reg_read(UC_X86_REG_EDX))[1], 1)
    machine.hook(0x6442A0, lambda c: (calls.append(("level", c.arg(0), c.arg(1), c.arg(2))),
                                                 levels[c.arg(1) - 0x9000])[1], 3)
    machine.w32(0x7C8CA0, 0)
    if machine.call(0x4ED480, player, limit=1000) != 0:
        raise AssertionError("FUN_004ed480 found an unlearned skill")
    expected_search = [("skill", player, 0, MASK), ("level", player, 0x9000, 0),
                       ("skill", player, 2, MASK), ("level", player, 0x9002, 0)]
    if calls != expected_search:
        raise AssertionError(f"FUN_004ed480 first search {calls}")
    levels[2] = 1
    calls.clear()
    if machine.call(0x4ED480, player, limit=1000) != 1 or machine.r32(0x7C8CA0) != 1:
        raise AssertionError("FUN_004ed480 missed the learned class skill")
    if calls != expected_search:
        raise AssertionError(f"FUN_004ed480 successful search {calls}")
    calls.clear()
    got = machine.call(0x4ED480, player, limit=100)
    if mutation == "learned-no-cache":
        if not calls:
            raise AssertionError("FUN_004ed480 unexpectedly used its cache")
    elif got != 1 or calls:
        raise AssertionError(f"FUN_004ed480 cache calls {calls}")


def check_attack_rating(machine):
    """FUN_004eda80 dispatches descatt 0..71 and returns the handler's first output word."""
    skill, row, stats = machine.alloc(0x20), machine.alloc(0x20), machine.alloc(0x20)
    selected, row_ok, handler_log = {"value": skill}, {"value": row}, []
    machine.hook(0x620190, lambda c: selected["value"], 1)
    machine.hook(0x6201D0, lambda c: selected["value"], 1)
    machine.hook(0x644140, lambda c: stats, 0)
    machine.hook(0x4A89D0, lambda c: row_ok["value"], 0)
    machine.hook(0x6442A0, lambda c: 9, 3)

    def handler(current):
        handler_log.append((current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                            tuple(current.arg(i) for i in range(6))))
        current.w32(current.arg(2), 0x12345678)
        return 0

    target = machine.alloc(0x10)
    machine.hook(target, handler, 6)
    player = machine.alloc(0x20)
    for hand in (0, 1):
        for descatt in (0, 1, 17, 18, 53, 54, 71, 72, 0xFFFF):
            machine.mu.mem_write(row + 0x14, struct.pack("<H", descatt))
            if descatt < 72:
                machine.w32(0x72D7F8 + descatt * 4, target)
            handler_log.clear()
            got = machine.call(0x4EDA80, ecx=player, edx=hand, limit=1000)
            correct = 0x12345678 if descatt < 72 else 0
            changed = 0x12345678 if descatt < 18 else 0
            if got != choose("rating-bound-18", correct, changed):
                raise AssertionError(f"FUN_004eda80 hand {hand} descatt {descatt}: {got:#x}")
            if (got != 0) != bool(handler_log):
                raise AssertionError(f"FUN_004eda80 handler log {handler_log}")
    if machine.call(0x4EDA80, ecx=0, edx=0, limit=100) != 0:
        raise AssertionError("FUN_004eda80 null player")
    selected["value"] = 0
    if machine.call(0x4EDA80, ecx=player, edx=0, limit=100) != 0:
        raise AssertionError("FUN_004eda80 null selected skill")
    selected["value"], row_ok["value"] = skill, 0
    if machine.call(0x4EDA80, ecx=player, edx=0, limit=200) != 0:
        raise AssertionError("FUN_004eda80 null SkillDesc row")


def check_connecting_screen(machine):
    """FUN_0042f6e0 builds three controls, enables them, starts drawing and installs callback 0x42f6c0."""
    log, handles = [], iter((0x1111, 0x2222, 0x3333))
    machine.hook(0x42F3F0, lambda c: (log.append(("clear",)), 0)[1], 0)
    machine.hook(0x42F3A0, lambda c: (log.append(("make",)), next(handles))[1], 0)
    machine.hook(0x4FA470, lambda c: (log.append(("hide", c.arg(0), c.arg(1))), 0)[1], 2)
    machine.hook(0x4FA420, lambda c: (log.append(("enable", c.arg(0), c.arg(1))), 0)[1], 2)
    machine.hook(0x524A30, lambda c: (log.append(("strings",)), 0)[1], 0)
    machine.hook(0x4FD060, lambda c: (log.append(("draw",)), 0)[1], 0)
    machine.w32(0x7795F0, 0)
    machine.w32(0x779954, 0)
    machine.w32(0x779974, MASK)
    machine.call(0x42F6E0, limit=1000)
    correct = [("clear",), ("make",), ("hide", 0x1111, 0), ("make",), ("make",),
               ("enable", 0x1111, 1), ("enable", 0x2222, 1), ("enable", 0x3333, 1),
               ("strings",), ("draw",)]
    changed = [entry for entry in correct if entry != ("enable", 0x3333, 1)]
    if log != choose("connecting-skips-third", correct, changed):
        raise AssertionError(f"FUN_0042f6e0 calls {log}")
    if (machine.r32(0x779954), machine.r32(0x779974), machine.r32(0x7795BC)) != (1, 0, 0x42F6C0):
        raise AssertionError("FUN_0042f6e0 globals")


def check_assign_player(machine):
    """FUN_0053e8f0 builds S->C 0x59, sends 26 bytes, then assigns the found unit."""
    sent, log = [], []

    def copy_string(current):
        destination, source, bound = (current.arg(i) for i in range(3))
        log.append(("copy", bound))
        blob = current.read(source, 64).split(b"\0", 1)[0] + b"\0"
        current.mu.mem_write(destination, blob)
        return destination

    machine.hook(0x4135D0, copy_string, 3)
    machine.hook(0x53B280, lambda c: (sent.append(current_blob(c, 0, 1)), 0)[1], 2)
    machine.hook(0x537860, lambda c: 0xA100, 2)
    machine.hook(0x5387C0, lambda c: (log.append(("lookup", c.mu.reg_read(UC_X86_REG_ECX), c.arg(0))), 0xB200)[1], 1)
    machine.hook(0x552F60, lambda c: (log.append(("find", c.mu.reg_read(UC_X86_REG_ECX), c.mu.reg_read(UC_X86_REG_EDX))), 0xC300)[1], 0)
    machine.hook(0x53DA90, lambda c: (log.append(("assign", c.mu.reg_read(UC_X86_REG_ECX), c.mu.reg_read(UC_X86_REG_EDX))), 0)[1], 0)
    machine.hook(0x681A48, lambda c: 0, 0)
    client = machine.alloc(0x20)
    for name_text in (b"a", b"abcdefghijklmno", b"abcdefghijklmnop", b"abcdefghijklmnopqrstu"):
        name = machine.put(name_text + b"\0")
        sent.clear()
        log.clear()
        machine.call(0x53E8F0, 7, name, 0x3456, 0x789A, ecx=client, edx=0x12345678, limit=2000)
        if len(sent) != 1:
            raise AssertionError("FUN_0053e8f0 did not send one packet")
        packet = sent[0]
        expected = bytearray(26)
        expected[0] = 0x59
        expected[1:5] = struct.pack("<I", 0x12345678)
        expected[5] = 7
        expected[6:6 + min(len(name_text), 16)] = name_text[:16]
        expected[22:24] = struct.pack("<H", 0x3456)
        expected[24:26] = struct.pack("<H", 0x789A)
        bound = choose("assign-player-short-bound", 0x7FFFFFFF, 16)
        if packet != bytes(expected) or ("copy", bound) not in log:
            raise AssertionError(f"FUN_0053e8f0 packet {packet.hex()} log {log}")
        if log[-3:] != [("lookup", client, 0x12345678), ("find", 0xB200, 0), ("assign", 0xA100, 0xC300)]:
            raise AssertionError(f"FUN_0053e8f0 follow-up {log}")


def current_blob(current, pointer_index, length_index):
    """Read a pointer/length pair from a hook call."""
    return current.read(current.arg(pointer_index), current.arg(length_index))


class FatalCall(RuntimeError):
    """A game fatal path reached in a synthetic oracle case."""


def check_save_serialize(machine):
    """FUN_00532340 serializes at most 0x2000 bytes, replaces upload state and finalizes it."""
    log, count, existing = [], {"value": 1}, {"value": 0}
    machine.hook(0x538E00, lambda c: existing["value"], 0)
    machine.hook(0x538F10, lambda c: (log.append(("clear",)), 0)[1], 0)

    def serialize(current):
        log.append(("serialize", current.arg(2), current.arg(3), current.arg(4)))
        current.mu.mem_write(current.arg(0), bytes(range(count["value"])))
        current.w32(current.arg(1), count["value"])
        return 0

    machine.hook(0x569AD0, serialize, 5)
    machine.hook(0x538590, lambda c: (log.append(("begin",)), 0)[1], 0)
    machine.hook(0x538CE0, lambda c: (log.append(("append", tuple(c.arg(i) for i in range(5)))), 0)[1], 5)
    machine.hook(0x538F80, lambda c: (log.append(("finish",)), 0)[1], 0)
    machine.hook(0x408A60, lambda c: (_ for _ in ()).throw(FatalCall("fatal")), 0)
    machine.hook(0x681A48, lambda c: 0, 0)
    client = machine.alloc(0x20)
    for old in (0, 1):
        for length in (1, 7, 31):
            existing["value"], count["value"] = old, length
            log.clear()
            machine.call(0x532340, client, limit=5000)
            correct = [("clear",)] if old else []
            correct += [("serialize", 0x2000, client, 0), ("begin",),
                        ("append", (length, length, 0, 0, 0)), ("finish",)]
            changed = [entry for entry in correct if entry != ("finish",)]
            if log != choose("save-skips-finish", correct, changed):
                raise AssertionError(f"FUN_00532340 calls {log}")
    count["value"] = 0
    try:
        machine.call(0x532340, client, limit=1000)
    except FatalCall:
        pass
    else:
        raise AssertionError("FUN_00532340 accepted an empty serialization")


def check_sound_pan(machine):
    """FUN_00515890 maps low pan directly, high pan as 255-pan, and midpoint to zero."""
    log, converted = [], {"value": 0x1357}

    def angle(current):
        bits = current.arg(2)
        value = struct.unpack("<f", struct.pack("<I", bits))[0]
        log.append(("angle", current.arg(0), current.arg(1), value))
        return 0

    machine.hook(0x5165F0, angle, 3)
    machine.hook(0x682FD0, lambda c: converted["value"], 0)
    machine.hook(0x408A60, lambda c: (_ for _ in ()).throw(FatalCall("null sound")), 0)
    vtable, obj, holder = machine.alloc(0x80), machine.alloc(0x20), machine.alloc(4)
    sink = machine.alloc(0x10)
    machine.w32(vtable + 0x40, sink)
    machine.w32(obj, vtable)
    machine.w32(holder, obj)
    machine.hook(sink, lambda c: (log.append(("set", c.arg(0), c.arg(1))), 0)[1], 2)
    for pan in (0, 1, 64, 127, 128, 129, 192, 254, 255):
        log.clear()
        machine.call(0x515890, ecx=holder, edx=pan, limit=1000)
        if pan == 128:
            expected = [("set", obj, 0)]
        else:
            amount = float(pan if pan < 128 else 255 - pan)
            if mutation == "pan-does-not-mirror" and pan > 128:
                amount = float(pan)
            expected = [("angle", 0, struct.unpack("<I", struct.pack("<f", 127.0))[0], amount),
                        ("set", obj, converted["value"])]
        if log != expected:
            raise AssertionError(f"FUN_00515890 pan {pan}: {log}")


def check_dual_line(machine):
    """FUN_004e9870 caches the language-dependent y offset and draws two values."""
    log, language = [], {"value": 0}
    machine.hook(0x502EF0, lambda c: (log.append(("font",)), 0)[1], 0)
    machine.hook(0x525150, lambda c: language["value"], 0)
    machine.hook(0x4A7080, lambda c: (log.append(("draw", c.arg(0), c.arg(1), c.arg(2))), 0)[1], 3)
    machine.w32(0x7A2858, 100)
    for lang, offset in ((0, -6), (5, -6), (6, -7), (9, -7), (10, -6)):
        machine.mu.mem_write(0x7C8C9C, b"\0")
        language["value"] = lang
        log.clear()
        machine.call(0x4E9870, 0x30, 0x40, 15, 0x50, 0x60, ecx=0x10, edx=0x20, limit=1000)
        expected_offset = choose("dual-line-y-minus-six", offset, -6)
        expected = [("font",), ("draw", 115, 0x10, 0x50), ("draw", 115, 0x20, 0x60)]
        if log != expected or machine.s32(0x72D8D4) != expected_offset:
            raise AssertionError(f"FUN_004e9870 language {lang}: {log}, y {machine.s32(0x72D8D4)}")


def check_remove_item(machine):
    """FUN_0057f410 validates inventory membership, clears wield state and refreshes owner/item state."""
    log, membership, is_special, was_wielded = [], {"value": 0}, {"value": 0}, {"value": 0}
    machine.hook(0x62A360, lambda c: was_wielded["value"], 2)
    machine.hook(0x63D2B0, lambda c: (log.append(("leaves", c.arg(0), c.arg(1))), 0)[1], 2)
    machine.hook(0x63AD90, lambda c: membership["value"], 2)
    machine.hook(0x63BE30, lambda c: (log.append(("slot", c.arg(0), c.arg(1))), 0)[1], 2)
    machine.hook(0x62B400, lambda c: is_special["value"], 1)
    machine.hook(0x5608C0, lambda c: (log.append(("special", c.arg(0))), 0)[1], 1)
    machine.hook(0x55C730, lambda c: (log.append(("owner", tuple(c.arg(i) for i in range(3)))), 0)[1], 3)
    machine.hook(0x6280D0, lambda c: (log.append(("flag", tuple(c.arg(i) for i in range(3)))), 0)[1], 3)
    machine.hook(0x628170, lambda c: (log.append(("mode", tuple(c.arg(i) for i in range(3)))), 0)[1], 3)
    machine.hook(0x63CC70, lambda c: (log.append(("unlink", c.arg(0), c.arg(1))), 0)[1], 2)
    machine.hook(0x55D930, lambda c: (log.append(("empty", c.arg(0))), 0)[1], 1)
    machine.hook(0x663CC0, lambda c: (log.append(("event", tuple(c.arg(i) for i in range(4)))), 0)[1], 4)
    machine.hook(0x621000, lambda c: (log.append(("refresh", c.arg(0), c.arg(1))), 0)[1], 2)
    machine.hook(0x408A60, lambda c: (_ for _ in ()).throw(FatalCall("bad item")), 0)
    owner, inventory, item = machine.alloc(0x100), machine.alloc(0x20), machine.alloc(0x100)
    machine.w32(owner + 0x60, inventory)
    for wielded in (0, 1):
        for special in (0, 0x13):
            was_wielded["value"], is_special["value"], membership["value"] = wielded, special, item
            machine.w32(item + 0xC4, 0xFFFFFFFF)
            log.clear()
            machine.call(0x57F410, 0xAAAA, 0xBBBB, regs={"edi": owner, "esi": item}, limit=3000)
            expected_word = 0xFFFFFFFF if mutation == "remove-keeps-bit" else 0xFFFFFFFD
            if machine.r32(item + 0xC4) != expected_word:
                raise AssertionError("FUN_0057f410 item bit")
            if ("slot", inventory, 0xBBBB) not in log or ("event", (owner, item, 1, 1)) not in log:
                raise AssertionError(f"FUN_0057f410 calls {log}")
            if bool(("empty", 0) in log) != bool(wielded):
                raise AssertionError(f"FUN_0057f410 wielded callback {log}")
            if bool(("special", 0) in log) != bool(special == 0x13):
                raise AssertionError(f"FUN_0057f410 special callback {log}")


def check_cain_reward(machine):
    """FUN_005e77a0 initializes one shared spot and chooses Cain's class-specific callbacks."""
    quest = machine.alloc(0x40)
    log, quest_present, found = [], {"value": quest}, {"value": 0}
    machine.hook(0x58EFA0, lambda c: quest_present["value"], 2)
    machine.hook(0x45ADF0, lambda c: 11, 0)
    machine.hook(0x45AE20, lambda c: 22, 0)
    machine.hook(0x5944B0, lambda c: (log.append(("find-default",)), found["value"])[1], 0)
    machine.hook(0x5944F0, lambda c: (log.append(("spawn-default",)), 0)[1], 0)
    machine.hook(0x58A940, lambda c: (log.append(("find-alt",)), found["value"])[1], 0)
    machine.hook(0x58A980, lambda c: (log.append(("spawn-alt",)), 0)[1], 0)
    machine.hook(0x627260, lambda c: (log.append(("stat", tuple(c.arg(i) for i in range(4)))), 0)[1], 4)
    machine.hook(0x5DDFC0, lambda c: (log.append(("event", tuple(c.arg(i) for i in range(3)))), 0)[1], 3)
    machine.hook(0x5DE080, lambda c: (log.append(("finish", c.arg(0))), 0)[1], 1)
    for class_id in (1, 0x20F):
        unit = machine.alloc(0x20)
        machine.w32(unit + 4, class_id)
        for already in (0, 1):
            machine.w32(quest + 0xC, 0)
            machine.w32(quest + 0x10, 0)
            machine.w32(quest + 0x14, 0)
            machine.w32(quest + 0x18, MASK)
            found["value"] = already
            log.clear()
            got = machine.call(0x5E77A0, 0, ecx=0, edx=unit, limit=2000)
            prefix = "default" if (class_id != 0x20F or mutation == "cain-always-default") else "alt"
            expected_first = (f"find-{prefix}",)
            if got != 1 or not log or log[0] != expected_first:
                raise AssertionError(f"FUN_005e77a0 class {class_id:#x}: {log}")
            if not already and (f"spawn-{prefix}",) not in log:
                raise AssertionError(f"FUN_005e77a0 missing spawn: {log}")
            if (machine.r32(quest + 0xC), machine.r32(quest + 0x10), machine.r32(quest + 0x14),
                    machine.r32(quest + 0x18)) != (14, 25, 1, 0):
                raise AssertionError("FUN_005e77a0 quest fields")
    quest_present["value"] = 0
    if machine.call(0x5E77A0, 0, ecx=0, edx=0, limit=200) != 0:
        raise AssertionError("FUN_005e77a0 missing quest")


def check_player_name(machine):
    """FUN_00452af0 widens a roster name, computes width and marks party/special labels."""
    log, width_value, relation = [], {"value": 73}, {"value": 0}
    source = machine.put(b"Alice\0")
    machine.hook(0x47A210, lambda c: source, 0)
    machine.hook(0x47A6B0, lambda c: width_value["value"], 0)
    local_player = machine.alloc(0x20)
    machine.w32(local_player + 0xC, 0x4444)
    machine.hook(0x463DD0, lambda c: local_player, 0)
    machine.hook(0x47A070, lambda c: relation["value"], 0)

    def widen(current):
        log.append(("widen", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX), current.arg(0)))
        return 0

    machine.hook(0x526F20, widen, 1)
    machine.hook(0x525150, lambda c: 0, 0)
    machine.hook(0x524A30, lambda c: 0x7777, 2)
    machine.hook(0x5272B0, lambda c: (log.append(("compose", current_regs(c), c.arg(0))), 0)[1], 1)
    unit, destination, width, flag = (machine.alloc(0x200) for _ in range(4))
    machine.w32(unit + 0xC, 0x3333)
    for mode in (0, 0x11):
        for related in (0, 1):
            machine.w32(unit + 0x10, mode)
            relation["value"] = related
            machine.w32(width, MASK)
            machine.w32(flag, MASK)
            log.clear()
            machine.call(0x452AF0, destination, 0x80, ecx=unit, regs={"eax": width, "ebx": flag}, limit=2000)
            expected_width = 0 if mode == 0x11 else (width_value["value"] << 7) // 100
            correct_flag = 1 if mode == 0x11 or related else 0
            changed_flag = 1 if mode == 0x11 else 0
            if machine.r32(width) != expected_width or machine.r32(flag) != choose("player-name-no-relation", correct_flag, changed_flag):
                raise AssertionError(f"FUN_00452af0 mode {mode:#x} related {related}")
            if not any(entry[0] == "widen" and entry[-1] == 0x80 for entry in log):
                raise AssertionError(f"FUN_00452af0 did not widen: {log}")
            if (mode == 0x11) != any(entry[0] == "compose" for entry in log):
                raise AssertionError(f"FUN_00452af0 compose: {log}")


def current_regs(current):
    """Return ECX and EDX at a hook."""
    return current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)


def run(exe):
    """Run every top-20 check with the active mutation."""
    machine = CheckedEmu(str(exe))
    rng = random.Random(0xB10C2020)
    stream_cases = check_stream_setter(machine, rng)
    check_nearby_count(machine, rng)
    check_client_callback(machine, rng)
    check_end_flag(machine, rng)
    check_sound_list(machine, rng)
    check_dual_wield(machine)
    check_assignable_key(machine)
    check_throw_mastery(machine, rng)
    check_room_expiry(machine)
    check_wanderer(machine)
    check_learned_skill(machine)
    check_attack_rating(machine)
    check_connecting_screen(machine)
    check_assign_player(machine)
    check_save_serialize(machine)
    check_sound_pan(machine)
    check_dual_line(machine)
    check_remove_item(machine)
    check_cain_reward(machine)
    check_player_name(machine)
    return f"ok: 20 Blocking functions ({stream_cases} stream cases, 512 keys, synthetic branch/call/data checks)"


def main():
    """CLI entry point."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    exe = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    print(run(exe))
    if args.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run(exe)
            except AssertionError:
                print(f"mutation {name}: failed as required")
                continue
            finally:
                mutation = None
            raise SystemExit(f"mutation {name} was not caught")


if __name__ == "__main__":
    main()

