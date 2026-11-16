# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify seven live ITEMS/ItemMode.cpp routines of game.exe 1.14d (0x55c..0x55e).

    python items_mode_early_live.py [--mutation-check]

Runs FUN_0055ca40, FUN_0055cf50, FUN_0055d560, FUN_0055df10, FUN_0055e8e0,
FUN_0055eb30 and FUN_0055edc0 on synthetic units.  Every subsystem call
(inventory grid, events, packets, flags, rooms) is hooked and logged with its
register and stack arguments; the sequences are compared with models written
from the decompile and the assembly.  Simple pure helpers (item-list walking
FUN_0063dfd0/FUN_0063dfa0, FUN_0055ca00, path x/y getters, FUN_0062bad0 where
named) run for real.  ``--mutation-check`` breaks one rule of each model and
requires the comparison to fail.
"""
import argparse
import itertools

import emu
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

LIMIT = 400000
SRC = 0x6E1164           # ".\ITEMS\ItemMode.cpp" string used as the assert file argument
mutation = None


class Fatal(Exception):
    """Raised by the hooked process exit after game.exe logged an assertion."""


def need(condition, message):
    """Fail the oracle with MESSAGE unless CONDITION holds."""
    if not condition:
        raise AssertionError(message)


def new_machine():
    """An emulator whose assertion path records the line number and raises Fatal."""
    machine = emu.Emu()
    asserted = []

    def line(current):
        asserted.append(current.arg(0))
        return 1

    def die(current):
        raise Fatal(asserted[-1] if asserted else None)

    machine.hook(0x408090, line, 1)
    machine.hook(0x408A60, lambda current: 0, 0)
    machine.hook(0x681E09, die, 1)
    return machine, asserted


def trace(machine, log, address, name, nstack, result=0, registers=()):
    """Hook ADDRESS: log (name, ecx/edx as listed, NSTACK stack words), return RESULT."""
    def run(current):
        regs = {"ecx": UC_X86_REG_ECX, "edx": UC_X86_REG_EDX}
        entry = (name,) + tuple(current.mu.reg_read(regs[reg]) for reg in registers)
        entry += tuple(current.arg(index) for index in range(nstack))
        log.append(entry)
        return result(current) if callable(result) else result

    machine.hook(address, run, nstack)


def unit(machine, kind=4, klass=0, mode=0, flags=0, path=0, data=None, inventory=0):
    """Allocate a unit with the fields these routines read."""
    pointer = machine.alloc(0x120)
    machine.w32(pointer, kind)
    machine.w32(pointer + 4, klass)
    machine.w32(pointer + 0x10, mode)
    machine.w32(pointer + 0x2C, path)
    machine.w32(pointer + 0x60, inventory)
    machine.w32(pointer + 0xC4, flags)
    machine.w32(pointer + 0x14, machine.alloc(0x100) if data is None else data)
    return pointer


# ---------------------------------------------------------------------------
def duplicate_rules():
    """FUN_0055ca40: unique-file and rune-group duplicates before a candidate."""
    machine = emu.Emu()
    state = machine.alloc(0xD00)
    table = machine.alloc(0x14C * 4)
    machine.w32(0x744304, state)
    machine.w32(state + 0xC24, table)
    machine.w32(state + 0xC28, 3)
    for row, (first, flags) in enumerate(((5, 4), (6, 0), (9, 4))):
        machine.mu.mem_write(table + row * 0x14C, first.to_bytes(2, "little"))
        machine.mu.mem_write(table + row * 0x14C + 0x2C, flags.to_bytes(1, "little"))
    info = {}
    rows = {}
    machine.hook(0x627E70, lambda current: info[current.arg(0)]["quality"], 1)
    machine.hook(0x629DA0, lambda current: info[current.arg(0)]["file"], 1)
    machine.hook(0x628250, lambda current: info[current.arg(0)]["page"], 1)
    machine.hook(0x6335F0, lambda current: rows[current.arg(0)], 1)

    def make(quality=0, file=0, page=0, group=0, code=0):
        item = unit(machine)
        machine.w32(item + 4, item)           # the class is a unique key for the row hook
        row = machine.alloc(0x200)
        machine.mu.mem_write(row + 0x12A, bytes([group]))
        machine.w32(row + 0x80, code)
        info[item] = {"quality": quality, "file": file, "page": page}
        rows[item] = row
        return item, row

    def chain(items):
        """Link items head-first the way FUN_0063dfa0 follows unit data +0x5c, +8."""
        for index, item in enumerate(items):
            following = items[index + 1] if index + 1 < len(items) else 0
            machine.w32(machine.r32(item + 0x14) + 0x64, following)

    def model(candidate, row, others):
        """The decompile's loop: returns 1 on a preceding duplicate, 0 otherwise."""
        wanted = -1
        cinfo = info[candidate]
        unique_ok = cinfo["quality"] == 7 and 0 <= cinfo["file"] < 3
        if unique_ok and machine.read(table + cinfo["file"] * 0x14C + 0x2C, 1)[0] & 4:
            wanted = int.from_bytes(machine.read(table + cinfo["file"] * 0x14C, 2), "little", signed=True)
        for other in others:
            if other == candidate:
                return 0
            oinfo = info[other]
            if wanted >= 0 and oinfo["page"] != 1 and oinfo["quality"] == 7 and oinfo["file"] == wanted:
                return 1
            orow = rows[other]
            group = machine.read(orow + 0x12A, 1)[0]
            if group and oinfo["page"] != 1 and group == machine.read(row + 0x12A, 1)[0]:
                mine, theirs = machine.r32(row + 0x80), machine.r32(orow + 0x80)
                pairs = {(0x2034336A, 0x20343367), (0x20343367, 0x2034336A), (0x20736B62, 0x20646B62),
                         (0x20646B62, 0x20736B62), (0x20333364, 0x20333367), (0x20333367, 0x20333364),
                         (0x20747368, 0x2066736D), (0x2066736D, 0x20747368), (0x20747368, 0x20706976),
                         (0x20706976, 0x20747368)}
                quad = {0x20316671, 0x20726871, 0x20796571, 0x20726271}
                if (mine == theirs or (mine, theirs) in pairs
                        or (mine == 0x20326671 and theirs in quad) or (theirs == 0x20326671 and mine in quad)):
                    return 1
        return 0

    count = 0
    rune = {"group": 2}
    cases = []
    for quality, file in ((7, 0), (7, 1), (7, 2), (5, 0), (7, 3)):
        cases.append(("unique", quality, file))
    for mine, theirs in ((0x20202020, 0x20202020), (0x2034336A, 0x20343367), (0x20736B62, 0x20646B62),
                         (0x20333364, 0x20333367), (0x20747368, 0x2066736D), (0x20747368, 0x20706976),
                         (0x20326671, 0x20316671), (0x20726871, 0x20326671), (0x20326671, 0x20796571),
                         (0x20726271, 0x20326671), (0x20316671, 0x20726871), (0x20747368, 0x20747369),
                         (0x20326671, 0x20326672)):
        cases.append(("rune", mine, theirs))
    for case in cases:
        if case[0] == "unique":
            candidate, row = make(case[1], case[2], group=0)
            matching, _ = make(7, 5, page=0)       # unique file index 5 is row 0's first word
            stash, _ = make(7, 5, page=1)
            other, _ = make(7, 6, page=0)
            for order in ((matching,), (stash,), (other,), (stash, matching), (other, candidate, matching)):
                items = list(order) + ([candidate] if candidate not in order else [])
                chain(items)
                got = machine.call(0x55CA40, candidate, regs={"eax": items[0], "ebx": row}, limit=LIMIT)
                want = model(candidate, row, items)
                if mutation == "unique-stash":
                    want = 1 if any(info[o]["page"] == 1 and info[o]["file"] == 5 for o in items[:items.index(candidate)]) else want
                need(got == want, f"0055ca40 unique {case} {got} != {want}")
                count += 1
        else:
            candidate, row = make(group=rune["group"], code=case[1])
            peer, _ = make(group=rune["group"], code=case[2])
            stash_peer, _ = make(group=rune["group"], page=1, code=case[2])
            wrong_group, _ = make(group=3, code=case[2])
            for items in ([peer, candidate], [stash_peer, candidate], [wrong_group, candidate], [candidate, peer]):
                chain(items)
                got = machine.call(0x55CA40, candidate, regs={"eax": items[0], "ebx": row}, limit=LIMIT)
                want = model(candidate, row, items)
                if mutation == "rune-group":
                    want = 1 if items[0] == wrong_group else want
                need(got == want, f"0055ca40 rune {case} {items} {got} != {want}")
                count += 1
    # An empty list and a non-item node end the walk with 0.
    candidate, row = make(group=0)
    need(machine.call(0x55CA40, candidate, regs={"eax": 0, "ebx": row}, limit=LIMIT) == 0, "0055ca40 empty")
    bad = unit(machine, kind=1)
    need(machine.call(0x55CA40, candidate, regs={"eax": bad, "ebx": row}, limit=LIMIT) == 0, "0055ca40 non-item")
    return count + 2


def pickup_ground_item():
    """FUN_0055cf50: cursor/permission gates, gold vs item pickup, and flag output."""
    count = 0
    for scenario in ("fatal-game", "fatal-player", "fatal-inventory", "cursor", "busy", "no-unit", "not-item",
                     "wrong-mode", "denied", "gold", "item"):
        machine, asserted = new_machine()
        log = []
        game, player = machine.alloc(0x100), machine.alloc(0x100)
        inventory = machine.alloc(0x40)
        machine.w32(player + 0x60, 0 if scenario == "fatal-inventory" else inventory)
        ground = unit(machine, mode=3 if scenario not in ("wrong-mode",) else 1)
        machine.w32(ground + 0xC, 77)
        machine.w32(ground + 0xC4, 0xFFFFFFFF)
        if scenario == "not-item":
            machine.w32(ground, 1)
        category = [4 if scenario == "gold" else 3]
        out = machine.alloc(4)
        machine.w32(out, 0x99)
        unit_for = {"no-unit": 0}
        trace(machine, log, 0x63C1E0, "cursor", 1, ground if scenario == "cursor" else 0)
        trace(machine, log, 0x535060, "busy", 0, 1 if scenario == "busy" else 0, ("ecx",))
        trace(machine, log, 0x55BF50, "check", 1, 0, ("ecx", "edx"))
        trace(machine, log, 0x552F60, "unit", 1, unit_for.get(scenario, ground), ("ecx", "edx"))
        trace(machine, log, 0x55CC90, "permit", 1, 0 if scenario == "denied" else 1, ("ecx", "edx"))
        trace(machine, log, 0x55C9A0, "message", 1)
        trace(machine, log, 0x620BB0, "room", 1, 0x4444)
        trace(machine, log, 0x61A270, "room-del", 3)
        trace(machine, log, 0x62B400, "category", 1, lambda current, kind=category[0]: kind)
        trace(machine, log, 0x55C850, "gold", 2)
        for address, name, stack in ((0x623830, "path-del", 1), (0x64C370, "collide-del", 1),
                                     (0x63C180, "cursor-set", 2), (0x624690, "mode", 2),
                                     (0x628170, "flag", 3), (0x628280, "page", 2), (0x621000, "owner", 2),
                                     (0x63CC70, "inventory-add", 2)):
            trace(machine, log, address, name, stack)
        trace(machine, log, 0x543D80, "notify", 1, 0, ("ecx", "edx"))
        trace(machine, log, 0x553380, "finish", 1, 0, ("ecx", "edx"))
        args = (ground if scenario != "no-unit" else 0x5555, out)
        ecx = 0 if scenario == "fatal-game" else game
        edx = 0 if scenario == "fatal-player" else player
        try:
            machine.call(0x55CF50, *args, ecx=ecx, edx=edx, limit=LIMIT)
            fatal = None
        except Fatal as error:
            fatal = error.args[0]
        names = [entry[0] for entry in log]
        if scenario.startswith("fatal"):
            wanted = {"fatal-game": 0x558, "fatal-player": 0x559, "fatal-inventory": 0x55E}[scenario]
            need(fatal == wanted, f"0055cf50 {scenario} {fatal}")
        else:
            need(fatal is None, f"0055cf50 {scenario} fatal")
        if scenario in ("cursor", "busy"):
            need(names == ["cursor"] + ([] if scenario == "cursor" else ["busy"]) and machine.r32(out) == 0, f"0055cf50 {scenario} {names}")
        elif scenario in ("no-unit", "not-item"):
            need(names == ["cursor", "busy", "check", "unit"] and machine.r32(out) == 0, f"0055cf50 {scenario} {names}")
        elif scenario == "wrong-mode":
            need(names == ["cursor", "busy", "check", "unit"] and machine.r32(out) == 1, f"0055cf50 {scenario} {names}")
        elif scenario == "denied":
            need(names == ["cursor", "busy", "check", "unit", "permit", "message"] and log[-1] == ("message", 0x13), f"0055cf50 {scenario} {log}")
        elif scenario == "gold":
            wanted = ["cursor", "busy", "check", "unit", "permit", "room", "room-del", "category", "gold", "finish"]
            need(names == wanted, f"0055cf50 gold {names}")
            need(log[6] == ("room-del", 0x4444, 4, 77) and log[8] == ("gold", game, ground), f"0055cf50 gold args {log}")
        elif scenario == "item":
            wanted = ["cursor", "busy", "check", "unit", "permit", "room", "room-del", "category", "path-del",
                      "collide-del", "cursor-set", "mode", "flag", "page", "owner", "inventory-add", "notify", "finish"]
            if mutation == "pickup-order":
                wanted[8], wanted[9] = wanted[9], wanted[8]
            need(names == wanted, f"0055cf50 item {names}")
            need(log[10] == ("cursor-set", inventory, ground) and log[11] == ("mode", ground, 4)
                 and log[12] == ("flag", ground, 0x40, 1) and log[13] == ("page", ground, 0xFF)
                 and log[14] == ("owner", player, 1) and log[15] == ("inventory-add", inventory, ground),
                 f"0055cf50 item args {log}")
            flags = machine.r32(ground + 0xC4)
            need(flags == 0xFFFFFFFF & ~0x2 & ~0x02000000, f"0055cf50 flags {flags:x}")
        count += 1
    return count


def equip_class_flags():
    """FUN_0055d560: ten classification flags from the owner class and item type tests."""
    machine, asserted = new_machine()
    types = [0x1B, 0x23, 5, 6, 0x33, 0x2D, 10, 0x43]
    current = {"types": set(), "two": 0, "throw": 0, "stat": 0}
    machine.hook(0x629BB0, lambda cur: 1 if cur.arg(1) in current["types"] else 0, 2)
    machine.hook(0x6289C0, lambda cur: current["two"], 1)
    machine.hook(0x62BA80, lambda cur: current["throw"], 1)
    machine.hook(0x625500, lambda cur: current["stat"] if cur.arg(1) == 0x7D else 99, 3)
    item = unit(machine)
    output = machine.alloc(0x40)
    count = 0
    for subset in range(1 << len(types)):
        current["types"] = {kind for bit, kind in enumerate(types) if subset >> bit & 1}
        for two, throw, stat in itertools.product((0, 1), (0, 1), (0, 3)):
            current.update(two=two, throw=throw, stat=stat)
            for owner_type, owner_class in ((0, 4), (0, 6), (0, 1), (1, 4)):
                if (subset * 7 + two * 3 + throw + stat + owner_class) % 11 and subset not in (0, 255):
                    continue
                owner = machine.alloc(8)
                machine.w32(owner, owner_type)
                machine.w32(owner + 4, owner_class)
                machine.mu.mem_write(output, b"\x77" * 0x40)
                machine.call(0x55D560, ecx=owner, regs={"ebx": item, "esi": output}, limit=LIMIT)
                got = [machine.r32(output + 4 * index) for index in range(10)]
                has = lambda kind: 1 if kind in current["types"] else 0
                player = 1 if owner_type == 0 and (owner_class == 4 or (owner_class == 6 and 0x43 in current["types"])) else 0
                want = [has(0x1B), has(0x23), has(5), has(6), has(0x33), has(0x2D), two, player,
                        1 if throw or stat else 0, has(10)]
                if mutation == "class-throwable":
                    want[8] = 1 if throw else 0
                need(got == want, f"0055d560 {sorted(current['types'])} {two}{throw}{stat} {owner_type}/{owner_class}: {got} != {want}")
                need(machine.read(output + 40, 8) == b"\x77" * 8, "0055d560 wrote past ten flags")
                count += 1
    for owner_arg, item_arg, wanted in ((1, 0, 0x675), (0, 1, 0x676)):
        owner = machine.alloc(8) if owner_arg else 0
        try:
            machine.call(0x55D560, ecx=owner, regs={"ebx": 0 if item_arg == 0 else item, "esi": output}, limit=LIMIT)
        except Fatal as error:
            need(error.args[0] == wanted, f"0055d560 assert {error.args[0]}")
        else:
            raise AssertionError("0055d560 expected assert")
        count += 1
    return count


def unlink_to_ground_notice():
    """FUN_0055df10: unlink an owned item, clear its page and optionally notify."""
    count = 0
    for notify, fatal_case in itertools.product((0, 1), (None, "game", "owner", "null-unlink", "mismatch")):
        machine, asserted = new_machine()
        log = []
        game = machine.alloc(0x100)
        machine.w32(game + 0x70, 1)
        inventory = machine.alloc(0x40)
        owner = unit(machine, kind=0, inventory=inventory)
        item = unit(machine, flags=0xFFFFFFFF)
        other = unit(machine)
        unlink = {"null-unlink": 0, "mismatch": other}.get(fatal_case, item)
        trace(machine, log, 0x63AD90, "unlink", 2, unlink)
        trace(machine, log, 0x628250, "page", 1, 6)
        trace(machine, log, 0x621050, "area", 3, 0x70)
        trace(machine, log, 0x45AE20, "w", 0, 0x1111, ("ecx",))   # cdecl: its three pushes stay for FUN_0063bcf0
        trace(machine, log, 0x45ADF0, "h", 0, 0x2222, ("ecx",))
        trace(machine, log, 0x63BCF0, "grid", 8)
        trace(machine, log, 0x628280, "set-page", 2)
        trace(machine, log, 0x5531C0, "client", 1, 0x3333, ("ecx", "edx"))
        trace(machine, log, 0x571600, "send", 0, 0, ("ecx", "edx"))
        trace(machine, log, 0x557FD0, "finish", 0, 0, ("ecx", "edx"))
        ecx = 0 if fatal_case == "game" else game
        edx = 0 if fatal_case == "owner" else owner
        try:
            machine.call(0x55DF10, item, notify, ecx=ecx, edx=edx, limit=LIMIT)
            raised = None
        except Fatal as error:
            raised = error.args[0]
        if fatal_case is None:
            need(raised is None, "0055df10 unexpected assert")
            names = [entry[0] for entry in log]
            wanted = ["unlink", "page", "area", "w", "h", "grid", "set-page"] + (["client", "send"] if notify else []) + ["finish"]
            if mutation == "notify-always":
                wanted = ["unlink", "page", "area", "w", "h", "grid", "set-page", "client", "send", "finish"]
            need(names == wanted, f"0055df10 sequence {names}")
            need(log[0] == ("unlink", inventory, item) and log[2][:2] == ("area", owner) and log[2][2] & 0xFF == 6 and log[2][3] == 1, f"0055df10 args {log[:3]}")
            need(log[5][1:7] == (SRC, 0xF7B, inventory, 0x2222, 0x1111, 0x70) and log[5][7] == 0 and log[5][8] & 0xFF == 6, f"0055df10 grid {log[5]}")
            need(log[6] == ("set-page", item, 0xFF) and log[-1] == ("finish", game, item), f"0055df10 end {log[6:]}")
            need(machine.r32(item + 0xC4) == 0xFFFFFFFD, "0055df10 item flag 2")
            if notify:
                need(log[7] == ("client", owner, SRC, 0xF7E) and log[8] == ("send", item, 0x3333), f"0055df10 notify {log[7:9]}")
        else:
            wanted = {"game": 0xF6F, "owner": 0xF70, "null-unlink": 0xF76, "mismatch": 0xF77}[fatal_case]
            need(raised == wanted, f"0055df10 {fatal_case} -> {raised}")
        count += 1
    return count


def ground_block_from_item():
    """FUN_0055e8e0: the 0x84-byte create block for an inventory item."""
    count = 0
    for kind in (0, 1, 2, 3, 4, 5):
        machine, asserted = new_machine()
        game = machine.alloc(0x100)
        machine.mu.mem_write(game + 0x78, (0x1234).to_bytes(2, "little"))
        path = machine.alloc(0x40)
        machine.w32(path + 0xC, 0x55)
        machine.w32(path + 0x10, 0x66)
        machine.mu.mem_write(path + 2, (0x77).to_bytes(2, "little"))
        machine.mu.mem_write(path + 6, (0x88).to_bytes(2, "little"))
        item = unit(machine, kind=kind, path=path)
        machine.w32(item + 0x20, 0x9999)
        machine.w32(item + 0x28, 0xABCD)
        trace(machine, [], 0x6281E0, "level", 1, 33)
        trace(machine, [], 0x627DF0, "seed", 1, 0xCAFE)
        output = machine.alloc(0x84)
        machine.mu.mem_write(output, b"\xEE" * 0x84)
        machine.call(0x55E8E0, 0x4242, regs={"eax": game, "edi": item, "esi": output}, limit=LIMIT)
        direct = kind == 2 or 3 < kind < 6
        x, y = (0x55, 0x66) if direct else (0x77, 0x88)
        if mutation == "block-position":
            x, y = y, x
        want = {0: 0, 4: 0, 8: game, 0xC: 33, 0x14: 0x4242, 0x18: 4, 0x1C: x, 0x20: y, 0x2C: 0,
                0x30: 2, 0x40: 2, 0x48: 0xABCD, 0x4C: 0xCAFE}
        for offset, value in want.items():
            if offset == 4:
                value = item + 0x20
            need(machine.r32(output + offset) == value, f"0055e8e0 kind {kind} +{offset:x}")
        need(machine.r16(output + 0x28) == 1 and machine.r16(output + 0x2A) == 0x1234, f"0055e8e0 words kind {kind}")
        need(all(machine.r32(output + offset) == 0 for offset in (0x10, 0x24, 0x34, 0x38, 0x3C, 0x44, 0x50, 0x80)),
             f"0055e8e0 zeroed fields kind {kind}")
        count += 1
    # A unit without a path gives 0/0 for non-direct kinds.
    machine, asserted = new_machine()
    game, output = machine.alloc(0x100), machine.alloc(0x84)
    item = unit(machine, kind=4, path=0)
    machine.hook(0x6281E0, lambda current: 1, 1)
    machine.hook(0x627DF0, lambda current: 2, 1)
    item = unit(machine, kind=1, path=0)
    machine.call(0x55E8E0, 7, regs={"eax": game, "edi": item, "esi": output}, limit=LIMIT)
    need(machine.r32(output + 0x1C) == 0 and machine.r32(output + 0x20) == 0, "0055e8e0 no path")
    return count + 1


def belt_swap():
    """FUN_0055eb30: cursor belt item replaces a belt-slot item; every guard and fatal."""
    count = 0
    scenarios = ("fatal-game", "fatal-player", "no-source", "bad-source-mode", "not-beltable", "no-target",
                 "bad-target-mode", "null-unlink", "mismatch", "place-fails", "insert-fails", "ok", "ok-skill")
    for scenario in scenarios:
        machine, asserted = new_machine()
        log = []
        game = machine.alloc(0x100)
        inventory = machine.alloc(0x40)
        player = unit(machine, kind=0, inventory=inventory)
        source = unit(machine, mode=4)
        target = unit(machine, mode=2, flags=0xFFFFFFFF)
        source_mode = 3 if scenario == "bad-source-mode" else 4
        machine.w32(source + 0x10, source_mode)
        machine.w32(source + 0xC4, 0xFFFFFFFF)
        machine.w32(target + 0x10, 3 if scenario == "bad-target-mode" else 2)
        found = {1: 0 if scenario == "no-source" else source, 2: 0 if scenario == "no-target" else target}

        def lookup(current, log=log, found=found):
            log.append(("unit", current.arg(0)))
            return found[current.arg(0)]

        trace(machine, log, 0x55BF50, "check", 1, 0, ("ecx", "edx"))
        machine.hook(0x552F60, lookup, 1)
        trace(machine, log, 0x62BAD0, "belt", 1, 0 if scenario == "not-beltable" else 1)
        unlink = 0 if scenario == "null-unlink" else (source if scenario == "mismatch" else target)
        trace(machine, log, 0x63AD90, "unlink", 2, unlink)
        trace(machine, log, 0x45ADF0, "x", 0, 0x2A, ("ecx",))
        trace(machine, log, 0x63C550, "belt-empty", 2)
        trace(machine, log, 0x63C180, "cursor-clear", 2)
        trace(machine, log, 0x55C6E0, "skill-out", 0, 0, ("ecx", "edx"))
        trace(machine, log, 0x55C730, "skill-in", 3, 0, ("ecx", "edx"))
        trace(machine, log, 0x624690, "mode", 2)
        trace(machine, log, 0x628170, "flag", 3)
        trace(machine, log, 0x63CC70, "inv-add", 2)
        trace(machine, log, 0x63C4F0, "belt-place", 3, 0 if scenario == "place-fails" else 1)
        trace(machine, log, 0x620470, "x-set", 2)
        trace(machine, log, 0x6204C0, "y-set", 2)
        trace(machine, log, 0x63B210, "insert", 3, 0 if scenario == "insert-fails" else 1)
        trace(machine, log, 0x55C270, "skill-add", 0, 0, ("ecx", "edx"))
        trace(machine, log, 0x62FF70, "stat-gate", 2, 1 if scenario == "ok-skill" else 0)
        trace(machine, log, 0x55C2C0, "stats", 2, 0, ("ecx", "edx"))
        trace(machine, log, 0x628280, "page", 2)
        trace(machine, log, 0x55DBC0, "requirements", 1, 0, ("ecx", "edx"))
        out = machine.alloc(4)
        machine.w32(out, 0x99)
        ecx = 0 if scenario == "fatal-game" else game
        edx = 0 if scenario == "fatal-player" else player
        try:
            got = machine.call(0x55EB30, 1, 2, out, ecx=ecx, edx=edx, limit=LIMIT)
            fatal = None
        except Fatal as error:
            got, fatal = None, error.args[0]
        names = [entry[0] for entry in log]
        if scenario == "fatal-game":
            need(fatal == 0x128A, f"0055eb30 {scenario} {fatal}")
        elif scenario == "fatal-player":
            need(fatal == 0x128B, f"0055eb30 {scenario} {fatal}")
        elif scenario in ("no-source", "bad-source-mode", "not-beltable", "no-target", "bad-target-mode"):
            need(fatal is None and got & 0xFF == 0, f"0055eb30 {scenario} returned {got}")
            need(machine.r32(out) == (1 if scenario == "bad-target-mode" else 0), f"0055eb30 {scenario} out")
            prefix = {"no-source": ["check", "unit"], "bad-source-mode": ["check", "unit"],
                      "not-beltable": ["check", "unit", "belt"], "no-target": ["check", "unit", "belt", "unit"],
                      "bad-target-mode": ["check", "unit", "belt", "unit"]}[scenario]
            need(names == prefix, f"0055eb30 {scenario} {names}")
        elif scenario in ("null-unlink", "mismatch"):
            need(fatal == (0x12B8 if scenario == "null-unlink" else 0x12B9), f"0055eb30 {scenario} {fatal}")
        else:
            common = ["check", "unit", "belt", "unit", "unlink", "x", "belt-empty", "cursor-clear", "skill-out",
                      "skill-in", "mode", "flag", "inv-add", "x", "belt-place"]
            if scenario == "place-fails":
                need(fatal == 0x12D8 and names == common, f"0055eb30 place-fails {fatal} {names}")
            elif scenario == "insert-fails":
                need(fatal == 0x12D4 and names == common + ["x", "x-set", "y-set", "insert"], f"0055eb30 insert {fatal} {names}")
            else:
                tail = ["x", "x-set", "y-set", "insert", "skill-add", "stat-gate"]
                tail += ["stats"] if scenario == "ok-skill" else []
                tail += ["mode", "page", "flag", "inv-add", "requirements"]
                if mutation == "belt-gate":
                    tail.remove("stat-gate")
                need(fatal is None and got == 1 and names == common + tail, f"0055eb30 {scenario} {names}")
                need(log[4] == ("unlink", inventory, target) and log[6] == ("belt-empty", inventory, 0x2A)
                     and log[7] == ("cursor-clear", inventory, target) and log[8] == ("skill-out", player, target)
                     and log[9] == ("skill-in", game, target, player, 0, 1), f"0055eb30 first half {log[4:10]}")
                need(log[14] == ("belt-place", inventory, source, 0x2A) and log[15] == ("x", target), f"0055eb30 place {log[14:16]}")
                need(log[16] == ("x-set", source, 0x2A) and log[17] == ("y-set", source, 0)
                     and log[18] == ("insert", inventory, source, 2), f"0055eb30 source {log[16:19]}")
                need(log[19] == ("skill-add", player, source), f"0055eb30 skill add {log[19]}")
                need(machine.r32(source + 0xC4) == 0xFFFFFFFD and machine.r32(target + 0xC4) == 0xFFFFFFFD,
                     "0055eb30 unit flag 2")
                need(log[-1] == ("requirements", game, player, 0), f"0055eb30 requirements {log[-1]}")
        count += 1
    return count


def belt_compaction():
    """FUN_0055edc0: pack one belt column toward the front, four rows deep."""
    count = 0
    machine, asserted = new_machine()
    log = []
    state = {"slots": {}, "flag": 0}
    inventory = machine.alloc(0x40)
    owner = unit(machine, kind=0, inventory=inventory)
    slots = [unit(machine) for _ in range(16)]

    def slot_item(current):
        log.append(("slot", current.arg(1)))
        return slots[current.arg(1)] if current.arg(1) in state["slots"] else 0

    machine.hook(0x63C7F0, slot_item, 2)
    trace(machine, log, 0x63C4F0, "place", 3)
    trace(machine, log, 0x6280D0, "flag-set", 3)
    trace(machine, log, 0x6280A0, "flag-test", 4, lambda current: state["flag"])
    trace(machine, log, 0x621000, "owner", 2)
    trace(machine, log, 0x63CC70, "add", 2)
    trace(machine, log, 0x63C550, "belt-empty", 2)
    for index in (0, 1, 2, 3, 4, 7, 0xFE):
        for pattern in range(16):
            for flag in (0, 1):
                column = index & 3
                state["slots"] = {column + 4 * row for row in range(4) if pattern >> row & 1}
                state["flag"] = flag
                log.clear()
                machine.call(0x55EDC0, index, ecx=0, edx=owner, limit=LIMIT)
                want, packed = [], 0
                for row in range(4):
                    slot = column + 4 * row
                    want.append(("slot", slot))
                    if slot in state["slots"]:
                        if packed != row:
                            item = slots[slot]
                            destination = column + 4 * packed
                            want += [("place", inventory, item, destination), ("flag-set", item, 0x400, 1),
                                     ("flag-set", item, 1, 1), ("flag-test", item, 0x4000, 0x1313, SRC)]
                            if flag:
                                want.append(("flag-set", item, 0x4000, 0))
                            want += [("owner", owner, 1), ("add", inventory, item), ("belt-empty", inventory, slot)]
                        packed += 1
                if mutation == "belt-pack" and len(want) > 4:
                    want = want[:-1]
                need(log == want, f"0055edc0 index {index} pattern {pattern} flag {flag}: {log} != {want}")
                count += 1
    return count


CHECKS = (duplicate_rules, pickup_ground_item, equip_class_flags, unlink_to_ground_notice,
          ground_block_from_item, belt_swap, belt_compaction)
MUTATIONS = {"unique-stash": duplicate_rules, "rune-group": duplicate_rules, "pickup-order": pickup_ground_item,
             "class-throwable": equip_class_flags, "notify-always": unlink_to_ground_notice,
             "block-position": ground_block_from_item, "belt-gate": belt_swap, "belt-pack": belt_compaction}


def run_all():
    """Run every scenario group and return how many cases passed."""
    return sum(check() for check in CHECKS)


def main():
    """Entry point: run the oracle, then the negative controls on request."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutation = None
    total = run_all()
    print(f"ok: {total} early ItemMode scenarios over 7 functions")
    if args.mutation_check:
        for name, check in MUTATIONS.items():
            mutation = name
            try:
                check()
            except AssertionError:
                print(f"mutation caught: {name}")
            else:
                raise AssertionError(f"mutation survived: {name}")


if __name__ == "__main__":
    main()
