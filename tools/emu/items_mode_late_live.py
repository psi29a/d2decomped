# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify ten live ITEMS/ItemMode.cpp routines of game.exe 1.14d (0x55f..0x563).

    python items_mode_late_live.py [--mutation-check]

Runs FUN_0055fa40, FUN_0055fcc0, FUN_00560420, FUN_00562250, FUN_00562590,
FUN_005628c0, FUN_00562c40, FUN_00563470, FUN_00563840 and FUN_00563fe0 on
synthetic units.  Subsystem calls (inventory grid, packets, flags, events,
creation) are hooked and logged with their register and stack arguments, and
the logged sequences are compared with models written from the decompile and
the assembly.  The item-list helpers FUN_0063dfd0/FUN_0063dfa0 run for real.
``--mutation-check`` breaks one rule of each model and requires a failure.
"""
import argparse
import itertools

import emu
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDI,
                               UC_X86_REG_EDX, UC_X86_REG_ESI)

LIMIT = 400000
SRC = 0x6E1164           # ".\ITEMS\ItemMode.cpp"
REGISTERS = {"eax": UC_X86_REG_EAX, "ebx": UC_X86_REG_EBX, "ecx": UC_X86_REG_ECX,
             "edi": UC_X86_REG_EDI, "edx": UC_X86_REG_EDX, "esi": UC_X86_REG_ESI}
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
    return machine


def trace(machine, log, address, name, nstack, result=0, registers=()):
    """Hook ADDRESS: log (name, registers as listed, NSTACK stack words), return RESULT."""
    def run(current):
        entry = (name,) + tuple(current.mu.reg_read(REGISTERS[reg]) for reg in registers)
        entry += tuple(current.arg(index) for index in range(nstack))
        log.append(entry)
        return result(current) if callable(result) else result

    machine.hook(address, run, nstack)


def unit(machine, kind=4, mode=0, flags=0, ident=None, inventory=0, page=0):
    """Allocate a unit; PAGE is returned by the hooked FUN_00628250 through ``pages``."""
    pointer = machine.alloc(0x120)
    machine.w32(pointer, kind)
    machine.w32(pointer + 0x0C, pointer & 0xFFFF if ident is None else ident)
    machine.w32(pointer + 0x10, mode)
    machine.w32(pointer + 0x2C, machine.alloc(0x40))
    machine.w32(pointer + 0x60, inventory)
    machine.w32(pointer + 0xC4, flags)
    machine.w32(pointer + 0x14, machine.alloc(0x100))
    return pointer


def chain(machine, items):
    """Link items head-first the way FUN_0063dfa0 follows item data +0x5c, +8."""
    for index, item in enumerate(items):
        machine.w32(machine.r32(item + 0x14) + 0x64, items[index + 1] if index + 1 < len(items) else 0)


class Rig:
    """One emulator with an owner, inventory and a log; hooks consult ``state``."""

    def __init__(self, owner_kind=0):
        self.machine = new_machine()
        machine = self.machine
        self.log = []
        self.state = {"pages": {}, "units": {}, "first": 0, "cursor": 0, "kinds": {}}
        self.game = machine.alloc(0x100)
        machine.w32(self.game + 0x70, 1)
        machine.w32(self.game + 0xA8, 1000)
        self.inventory = machine.alloc(0x40)
        self.owner = unit(machine, kind=owner_kind, inventory=self.inventory)
        state = self.state
        machine.hook(0x628250, lambda current: state["pages"].get(current.arg(0), 0), 1)
        machine.hook(0x63B2C0, lambda current: state["first"], 1)
        machine.hook(0x63E020, lambda current: state["kinds"].get(current.arg(0), 0), 1)

    def hook(self, address, name, nstack, result=0, registers=()):
        """Log and stub one callee."""
        trace(self.machine, self.log, address, name, nstack, result, registers)

    def fatal_call(self, *call, **kwargs):
        """Run a call and return (eax, assertion line or None)."""
        try:
            return self.machine.call(*call, limit=LIMIT, **kwargs), None
        except Fatal as error:
            return None, error.args[0]


# ---------------------------------------------------------------------------
def tome_rebuild():
    """FUN_0055fa40: reset skill counts of stored items, then re-add the page-0 ones."""
    count = 0
    rig = Rig()
    machine, state = rig.machine, rig.state
    rig.hook(0x55F9C0, "reset", 0, 0, ("ecx", "edx"))
    rig.hook(0x55C110, "add", 1, 0, ("eax", "edi"))
    for modes, pages in itertools.product(itertools.product((0, 1), repeat=3), itertools.product((0, 1), repeat=3)):
        items = [unit(machine, mode=mode) for mode in modes]
        chain(machine, items)
        state["first"] = items[0]
        state["pages"] = {item: page for item, page in zip(items, pages)}
        rig.log.clear()
        machine.call(0x55FA40, ecx=rig.game, edx=rig.owner, limit=LIMIT)
        want = [("reset", rig.owner, item) for item, mode in zip(items, modes) if mode == 0]
        stored = [item for item, mode, page in zip(items, modes, pages) if mode == 0 and page == 0]
        if mutation == "rebuild-all-pages":
            stored = [item for item, mode in zip(items, modes) if mode == 0]
        want += [("add", item, rig.owner, 1) for item in stored]
        need(rig.log == want, f"0055fa40 modes {modes} pages {pages}: {rig.log} != {want}")
        count += 1
    state["first"] = 0
    rig.log.clear()
    machine.call(0x55FA40, ecx=rig.game, edx=rig.owner, limit=LIMIT)
    need(rig.log == [], "0055fa40 empty inventory")
    return count + 1


def unequip_all():
    """FUN_0055fcc0: remove every kind-3 and contributing kind-1 inventory entry."""
    count = 0
    for kind_pattern in list(itertools.product((0, 1, 3), repeat=2)) + [(3, 1, 0), (1, 3, 1)]:
        for contributes in (0, 1):
            for failure in (None, "null", "mismatch"):
                if failure and kind_pattern != (3, 1, 0):
                    continue
                rig = Rig()
                machine, state = rig.machine, rig.state
                items = [unit(machine) for _ in kind_pattern]
                chain(machine, items)
                state["first"] = items[0]
                state["kinds"] = dict(zip(items, kind_pattern))
                rig.hook(0x62FF70, "contributes", 2, contributes)
                rig.hook(0x62A360, "empty", 2)
                rig.hook(0x63D2B0, "wield-leaves", 2)

                def unlink(current, rig=rig, failure=failure):
                    item = current.arg(1)
                    return 0 if failure == "null" else (rig.owner if failure == "mismatch" else item)

                rig.hook(0x63AD90, "unlink", 2, unlink)
                rig.hook(0x63C110, "value", 2, 0x77)
                rig.hook(0x63BE30, "grid-empty", 2)
                rig.hook(0x55C730, "skill", 3, 0, ("ecx", "edx"))
                rig.hook(0x55DBC0, "requirements", 1, 0, ("ecx", "edx"))
                rig.hook(0x63E0B0, "reset", 1)
                got, fatal = rig.fatal_call(0x55FCC0, ecx=rig.game, edx=rig.owner)
                want = []
                for item, kind in zip(items, kind_pattern):
                    if kind == 3 or (kind == 1 and contributes):
                        want += [("contributes", item, rig.owner)] if kind == 1 else []
                        want += [("empty", rig.owner, item), ("wield-leaves", rig.inventory, item),
                                 ("unlink", rig.inventory, item)]
                        if failure:
                            break
                        if kind == 3:
                            want += [("value", rig.inventory, item), ("grid-empty", rig.inventory, 0x77)]
                        want += [("skill", rig.game, item, rig.owner, 0, 1), ("requirements", rig.game, rig.owner, 0)]
                    elif kind == 1:
                        want += [("contributes", item, rig.owner)]
                if mutation == "unequip-kind":
                    want = [entry for entry in want if entry[0] != "value"]
                if failure:
                    need(fatal == (0x1A0F if failure == "null" else 0x1A10), f"0055fcc0 {failure} {fatal}")
                else:
                    want.append(("reset", rig.inventory))
                    need(fatal is None, f"0055fcc0 unexpected assert {fatal}")
                need(rig.log == want, f"0055fcc0 {kind_pattern} {contributes} {failure}: {rig.log} != {want}")
                count += 1
    for owner_case in ("no-owner", "no-inventory"):
        rig = Rig()
        owner = 0 if owner_case == "no-owner" else unit(rig.machine)
        rig.hook(0x63E0B0, "reset", 1)
        rig.machine.call(0x55FCC0, ecx=rig.game, edx=owner, limit=LIMIT)
        need(rig.log == [], f"0055fcc0 {owner_case}")
        count += 1
    # A null item in an inventory node asserts 0x19fd.
    rig = Rig()
    bad = unit(rig.machine, kind=1)
    rig.state["first"] = bad
    got, fatal = rig.fatal_call(0x55FCC0, ecx=rig.game, edx=rig.owner)
    need(fatal == 0x19FD, f"0055fcc0 null item {fatal}")
    return count + 1


def cursor_pickup():
    """FUN_00560420: stored item to the cursor, with the inventory override and gates."""
    count = 0
    rig = Rig()
    machine, state, log = rig.machine, rig.state, rig.log
    item = unit(machine, mode=0, flags=0xFFFFFFFF)
    other_inventory = machine.alloc(0x40)
    st = {"unit": item, "cursor": 0, "busy": 0, "contrib": 0, "unlink": item, "flag": 0, "ground": 0, "has": 0}
    rig.hook(0x63C1E0, "cursor", 1, lambda current: st["cursor"])
    rig.hook(0x552F60, "unit", 1, lambda current: st["unit"], ("ecx", "edx"))
    rig.hook(0x535060, "busy", 0, lambda current: st["busy"], ("ecx",))
    rig.hook(0x55BF50, "check", 1, 0, ("ecx", "edx"))
    rig.hook(0x63AD90, "unlink", 2, lambda current: st["unlink"])
    rig.hook(0x55C730, "skill", 3, 0, ("ecx", "edx"))
    rig.hook(0x62FF70, "contributes", 2, lambda current: st["contrib"])
    rig.hook(0x55DBC0, "requirements", 1, 0, ("ecx", "edx"))
    rig.hook(0x621000, "owner", 2)
    rig.hook(0x55C6E0, "skill-out", 0, 0, ("ecx", "edx"))
    rig.hook(0x621050, "area", 3, 0x70)
    rig.hook(0x45AE20, "w", 0, 0x1111, ("ecx",))
    rig.hook(0x45ADF0, "h", 0, 0x2222, ("ecx",))
    rig.hook(0x63BCF0, "grid", 8)
    rig.hook(0x628320, "set-old-page", 2)
    rig.hook(0x628280, "page", 2)
    rig.hook(0x63C180, "cursor-set", 2)
    rig.hook(0x624690, "mode", 2)
    rig.hook(0x6280D0, "flag-set", 3)
    rig.hook(0x567400, "cube", 1, 0, ("ecx", "edx"))
    rig.hook(0x628170, "transient", 3)
    rig.hook(0x55F590, "dirty", 1, lambda current: st["has"], ("ecx", "edx"))
    rig.hook(0x6280A0, "flag-test", 4, lambda current: st["flag"])
    rig.hook(0x63CC70, "inv-add", 2)
    for page_value, owner_kind, override, flag, pickup, busy, contrib in itertools.product((0, 1, 2), (0, 1), (0, 1), (0, 1), (1, 2), (0, 1), (0, 1)):
        owner = rig.owner
        machine.w32(owner, owner_kind)
        state["pages"] = {item: page_value}
        for p5, p6, p8 in ((1, 0, 0), (1, 1, 0), (0, 0, 0)):
            machine.w32(item + 0xC4, 0xFFFFFFFF)
            st.update(flag=flag, has=pickup == 2, contrib=contrib, busy=busy)
            log.clear()
            out = machine.alloc(4)
            machine.w32(out, 0x55)
            inventory = other_inventory if override else 0
            if owner_kind == 0 and page_value != 0 and not busy:
                # The player's own page gate: busy==0 and page nonzero -> output 1, return 0.
                expected_gate = True
            else:
                expected_gate = False
            got = machine.call(0x560420, 0x4242, out, p5, p6, inventory, p8, ecx=rig.game, edx=owner, limit=LIMIT)
            inv = other_inventory if override else rig.inventory
            want = [("cursor", inv), ("unit", rig.game, 4, 0x4242)]
            if owner_kind == 0:
                want += [("busy", owner)]
            if expected_gate:
                need(got == 0 and machine.r32(out) == 1 and log == want, f"00560420 gate {log}")
                count += 1
                continue
            want += [("check", owner, SRC, 0x936), ("unlink", inv, item), ("skill", rig.game, item, owner, 0, 1),
                     ("contributes", item, owner)]
            if contrib:
                want += [("requirements", rig.game, owner, 0), ("owner", owner, 1)]
            want += [("skill-out", owner, item), ("area", owner, page_value, 1),
                     ("w", item), ("h", item), ("grid", SRC, 0x965, inv, 0x2222, 0x1111, 0x70, 0, page_value),
                     ("set-old-page", item, page_value), ("page", item, 0xFF)]
            # The cursor is set unless the owner is not a player or the player's page is 1.
            put_cursor = owner_kind == 0 and page_value != 1
            if mutation == "pickup-page1-cursor":
                put_cursor = owner_kind == 0
            if put_cursor:
                want += [("cursor-set", inv, item)]
            want += [("mode", item, 4)]
            if p5 == 1:
                if p6:
                    want += [("flag-set", item, 0x20, 1)]
                if owner_kind == 0 and page_value == 2:
                    want += [("cube", rig.game, owner, item)]
                want += [("transient", item, 4, 1), ("dirty", item, SRC, 0x975)]
                if st["has"]:
                    want += [("flag-set", item, 1, 1)]
                want += [("flag-test", item, 0x4000, 0x978, SRC)]
                if flag:
                    want += [("flag-set", item, 0x4000, 0)]
                want += [("inv-add", inv, item), ("owner", owner, 1)]
            need(got == 1, f"00560420 return {got}")
            norm = [entry if entry[0] != "area" else entry[:2] + (entry[2] & 0xFF,) + entry[3:] for entry in log]
            norm = [entry if entry[0] not in ("set-old-page", "grid") else entry[:-1] + (entry[-1] & 0xFF,) for entry in norm]
            wnorm = [entry if entry[0] != "area" else entry[:2] + (entry[2] & 0xFF,) + entry[3:] for entry in want]
            need(norm == wnorm, f"00560420 owner {owner_kind} page {page_value} {p5}{p6}: {norm} != {wnorm}")
            need(machine.r32(item + 0xC4) == 0xFFFFFFFD, "00560420 unit flag 2")
            count += 1
    # Gates and assertions.
    for scenario in ("occupied", "occupied-allowed", "no-unit", "not-item", "busy-page", "wrong-mode", "null-unlink",
                     "mismatch", "fatal-game", "fatal-owner", "fatal-inventory"):
        rig.log.clear()
        machine.w32(rig.owner, 0)
        machine.w32(item + 0xC4, 0xFFFFFFFF)
        machine.w32(item + 0x10, 3 if scenario == "wrong-mode" else 0)
        machine.w32(item, 1 if scenario == "not-item" else 4)
        st.update(cursor=item if scenario.startswith("occupied") else 0, unit=0 if scenario == "no-unit" else item,
                  unlink=0 if scenario == "null-unlink" else (rig.owner if scenario == "mismatch" else item),
                  busy=0, contrib=0)
        state["pages"] = {item: 1 if scenario == "busy-page" else 0}
        out = machine.alloc(4)
        machine.w32(out, 0x55)
        game = 0 if scenario == "fatal-game" else rig.game
        owner = 0 if scenario == "fatal-owner" else rig.owner
        machine.w32(rig.owner + 0x60, 0 if scenario == "fatal-inventory" else rig.inventory)
        got, fatal = rig.fatal_call(0x560420, 1, out, 1, 0, 0, 1 if scenario == "occupied-allowed" else 0,
                                    ecx=game, edx=owner)
        machine.w32(rig.owner + 0x60, rig.inventory)
        if scenario == "fatal-game":
            need(fatal == 0x906, f"00560420 {scenario} {fatal}")
        elif scenario == "fatal-owner":
            need(fatal == 0x907, f"00560420 {scenario} {fatal}")
        elif scenario == "fatal-inventory":
            need(fatal == 0x910, f"00560420 {scenario} {fatal}")
        elif scenario == "occupied":
            need(fatal is None and got == 0 and machine.r32(out) == 0 and [e[0] for e in rig.log] == ["cursor"],
                 f"00560420 {scenario} {got} {rig.log}")
        elif scenario == "no-unit":
            need(got == 0 and machine.r32(out) == 1, f"00560420 {scenario} {got}")
        elif scenario == "not-item":
            need(got == 0 and machine.r32(out) == 0 and [e[0] for e in rig.log] == ["cursor", "unit"], f"00560420 {scenario} {got}")
        elif scenario == "busy-page":
            need(got == 0 and machine.r32(out) == 1, f"00560420 {scenario}")
        elif scenario == "wrong-mode":
            need(got == 0 and machine.r32(out) == 1, f"00560420 {scenario}")
        elif scenario in ("null-unlink", "mismatch"):
            need(fatal == (0x947 if scenario == "null-unlink" else 0x948), f"00560420 {scenario} {fatal}")
        elif scenario == "occupied-allowed":
            need(fatal is None and got == 1, f"00560420 {scenario} {got}")
        count += 1
    machine.w32(item, 4)
    return count


def belt_pickup():
    """FUN_00562250: belted item to the cursor, then compact the belt column."""
    count = 0
    for scenario in ("fatal-game", "fatal-player", "fatal-inventory", "occupied", "no-unit", "not-item", "wrong-mode", "ok"):
        rig = Rig()
        machine = rig.machine
        item = unit(machine, mode=3 if scenario == "wrong-mode" else 2, flags=0xFFFFFFFF)
        machine.w32(item, 1 if scenario == "not-item" else 4)
        rig.hook(0x63C1E0, "cursor", 1, item if scenario == "occupied" else 0)
        rig.hook(0x552F60, "unit", 1, 0 if scenario == "no-unit" else item, ("ecx", "edx"))
        rig.hook(0x55BF50, "check", 1, 0, ("ecx", "edx"))
        rig.hook(0x45ADF0, "x", 0, 0x2A, ("ecx",))
        rig.hook(0x63AD90, "unlink", 2)
        rig.hook(0x63C550, "belt-empty", 2)
        rig.hook(0x63C180, "cursor-set", 2)
        rig.hook(0x55C6E0, "skill-out", 0, 0, ("ecx", "edx"))
        rig.hook(0x55C730, "skill", 3, 0, ("ecx", "edx"))
        rig.hook(0x624690, "mode", 2)
        rig.hook(0x628170, "transient", 3)
        rig.hook(0x621000, "owner", 2)
        rig.hook(0x63CC70, "inv-add", 2)
        rig.hook(0x55EDC0, "compact", 1, 0, ("edx",))
        out = machine.alloc(4)
        machine.w32(out, 0x55)
        machine.w32(rig.owner + 0x60, 0 if scenario == "fatal-inventory" else rig.inventory)
        got, fatal = rig.fatal_call(0x562250, 9, out, ecx=0 if scenario == "fatal-game" else rig.game,
                                    edx=0 if scenario == "fatal-player" else rig.owner)
        names = [entry[0] for entry in rig.log]
        if scenario.startswith("fatal"):
            need(fatal == {"fatal-game": 0x1249, "fatal-player": 0x124A, "fatal-inventory": 0x124F}[scenario],
                 f"00562250 {scenario} {fatal}")
        elif scenario == "occupied":
            need(got == 0 and machine.r32(out) == 0 and names == ["cursor"], f"00562250 occupied {got} {names}")
        elif scenario == "no-unit":
            need(got == 0 and machine.r32(out) == 1 and names == ["cursor", "unit"], f"00562250 no-unit {got}")
        elif scenario == "not-item":
            need(got == 0 and machine.r32(out) == 0 and names == ["cursor", "unit"], f"00562250 not-item {got}")
        elif scenario == "wrong-mode":
            need(got == 0 and machine.r32(out) == 1 and names == ["cursor", "unit"], f"00562250 wrong-mode {got}")
        else:
            want = ["cursor", "unit", "check", "x", "unlink", "belt-empty", "cursor-set", "skill-out", "skill", "mode",
                    "transient", "owner", "inv-add", "compact"]
            if mutation == "belt-pickup-order":
                want[4], want[5] = want[5], want[4]
            need(got == 1 and names == want, f"00562250 ok {got} {names}")
            need(rig.log[5] == ("belt-empty", rig.inventory, 0x2A) and rig.log[-1] == ("compact", rig.owner, 0x2A)
                 and rig.log[10] == ("transient", item, 0x800, 1) and rig.log[9] == ("mode", item, 4),
                 f"00562250 args {rig.log}")
            need(machine.r32(item + 0xC4) == 0xFFFFFFFD, "00562250 unit flag 2")
        count += 1
    return count


def identify_item():
    """FUN_00562590: identify an item unless it already is or the owner has no inventory."""
    count = 0
    for flagged, has_inventory, transient, flag4000 in itertools.product((0, 1), (0, 1), (0, 1), (0, 1)):
        rig = Rig()
        machine = rig.machine
        item = unit(machine)
        machine.w32(rig.owner + 0x60, rig.inventory if has_inventory else 0)
        rig.hook(0x6280A0, "flag-test", 4, lambda current, flagged=flagged, flag4000=flag4000: flagged if current.arg(1) == 0x10 else flag4000)
        rig.hook(0x628140, "transient", 2, transient)
        rig.hook(0x55FE00, "identify", 1, 0, ("ecx", "edx"))
        rig.hook(0x6280D0, "flag-set", 3)
        rig.hook(0x63CC70, "inv-add", 2)
        rig.hook(0x621000, "owner", 2)
        rig.hook(0x553380, "notify", 1, 0, ("ecx", "edx"))
        got, fatal = rig.fatal_call(0x562590, item, ecx=rig.game, edx=rig.owner)
        names = [entry[0] for entry in rig.log]
        if flagged or not has_inventory:
            need(fatal is None and names == ["flag-test"], f"00562590 skip {names}")
        elif transient:
            need(fatal == 0x1383 and names == ["flag-test", "transient"], f"00562590 assert {fatal} {names}")
        else:
            want = ["flag-test", "transient", "identify", "flag-set", "flag-test"]
            want += ["flag-set"] if flag4000 else []
            want += ["inv-add", "owner", "notify"]
            if mutation == "identify-order":
                want[2], want[3] = want[3], want[2]
            need(fatal is None and names == want, f"00562590 ok {names}")
            need(rig.log[2] == ("identify", rig.game, item, rig.owner) and rig.log[3] == ("flag-set", item, 1, 1)
                 and rig.log[-1] == ("notify", rig.owner, 6, rig.owner), f"00562590 args {rig.log}")
        count += 1
    return count


def cube_insert():
    """FUN_005628c0: move an item into an owned stored cube (code 'box ')."""
    count = 0
    scenarios = ("fatal-game", "fatal-owner", "no-cube", "cube-mode", "cube-code", "restricted", "no-source",
                 "source-not-item", "source-mode", "cube-not-item", "ok-3", "ok-4")
    for scenario in scenarios:
        rig = Rig()
        machine = rig.machine
        cube = unit(machine, mode=1 if scenario == "cube-mode" else 0)
        machine.w32(cube, 1 if scenario == "cube-not-item" else 4)
        source = unit(machine, mode={"source-mode": 1, "ok-3": 3, "ok-4": 4}.get(scenario, 3), ident=0x3131)
        machine.w32(source, 1 if scenario == "source-not-item" else 4)
        rig.hook(0x55BF50, "check", 1, 0, ("ecx", "edx"))

        def lookup(current, cube=cube, source=source, scenario=scenario, rig=rig):
            rig.log.append(("unit", current.arg(0)))
            if current.arg(0) == 0xC0:
                return 0 if scenario == "no-cube" else cube
            return 0 if scenario == "no-source" else source

        rig.machine.hook(0x552F60, lookup, 1)
        rig.hook(0x628590, "code", 1, 0x20786F61 if scenario == "cube-code" else 0x20786F62)
        rig.hook(0x5678A0, "restricted", 1, 1 if scenario == "restricted" else 0, ("ecx", "edx"))
        rig.state["pages"] = {cube: 1 if scenario == "restricted" else 0}
        rig.hook(0x553380, "denied", 1, 0, ("ecx", "edx"))
        rig.hook(0x628280, "page", 2)
        rig.hook(0x560200, "place", 8, 1, ("ecx", "edx"))
        out = machine.alloc(4)
        machine.w32(out, 0x55)
        got, fatal = rig.fatal_call(0x5628C0, source and 0xA0, 0xC0, out,
                                    ecx=0 if scenario == "fatal-game" else rig.game,
                                    edx=0 if scenario == "fatal-owner" else rig.owner)
        names = [entry[0] for entry in rig.log]
        flag = machine.r32(out)
        if scenario == "fatal-game":
            need(fatal == 0x14A7, f"005628c0 {scenario} {fatal}")
        elif scenario == "fatal-owner":
            need(fatal == 0x14A8, f"005628c0 {scenario} {fatal}")
        elif scenario in ("no-cube", "cube-mode", "cube-code"):
            expected_names = {"no-cube": ["check", "unit"], "cube-mode": ["check", "unit"],
                              "cube-code": ["check", "unit", "code"]}[scenario]
            need(got == 0 and flag == 1 and names == expected_names, f"005628c0 {scenario} {got} {flag} {names}")
        elif scenario == "cube-not-item":
            need(got == 0 and flag == 0 and names[-1] == "unit", f"005628c0 {scenario} {got} {flag} {names}")
        elif scenario == "restricted":
            need(got == 0 and flag == 0 and names == ["check", "unit", "code", "restricted", "denied"]
                 and rig.log[-1] == ("denied", rig.owner, 0x13, rig.owner), f"005628c0 restricted {names} {rig.log[-1]}")
        elif scenario == "no-source":
            need(got == 0 and flag == 1, f"005628c0 no-source {got} {flag}")
        elif scenario == "source-not-item":
            need(got == 0 and flag == 0, f"005628c0 source-not-item {got} {flag}")
        elif scenario == "source-mode":
            need(got == 0 and flag == 1, f"005628c0 source-mode {got} {flag}")
        elif scenario.startswith("ok"):
            want = ["check", "unit", "code", "restricted", "unit", "page", "place"]
            if mutation == "cube-page":
                want = want[:5] + want[6:]
            need(got == 1 and names == want, f"005628c0 {scenario} {names}")
            need(rig.log[5] == ("page", source, 3) and rig.log[6] == ("place", SRC, 0x14EC, rig.game, rig.owner, 0x3131, 0, 0, 1, 1, 0),
                 f"005628c0 {scenario} args {rig.log[5:]}")
        count += 1
    return count


def replenish_worker():
    """FUN_00562c40: add one to a replenished stat, notify, and schedule the next event."""
    count = 0
    for rate, value, maximum, broken, owner_state in itertools.product((0, 1, 3, 2500, 9000, -5), (0, 4, 9), (5, 10), (0, 1),
                                                                      ("none", "no-unit", "no-client", "ok")):
        rig = Rig()
        machine, log = rig.machine, rig.log
        item = unit(machine)
        stat_values = {0xFC: rate, 0x48: value}

        def stat(current, stat_values=stat_values):
            return stat_values[current.arg(1)] & 0xFFFFFFFF

        machine.hook(0x625480, lambda current, stat=stat: stat(current), 3)
        rig.hook(0x627260, "set", 4)
        rig.hook(0x629F20, "owner-id", 1, 0xFFFFFFFF if owner_state == "none" else 0x1D)
        rig.hook(0x552F60, "unit", 1, 0 if owner_state == "no-unit" else rig.owner, ("ecx", "edx"))
        rig.hook(0x5531C0, "client", 1, 0 if owner_state == "no-client" else 0x9000, ("ecx", "edx"))
        rig.hook(0x53D130, "send", 4, 0, ("ecx", "edx"))
        rig.hook(0x6280A0, "broken", 4, broken)
        rig.hook(0x55F900, "repair", 1, 0, ("ecx", "edx"))
        rig.hook(0x5417D0, "event", 4, 0, ("ecx", "edx"))
        got = machine.call(0x562C40, rig.game, maximum, regs={"eax": 0xFC, "ebx": 0x48, "edi": item}, limit=LIMIT)
        need(got & 0xFF == (0 if rate == 0 else 1), f"00562c40 return {got}")
        if rate == 0:
            need(log == [], "00562c40 zero rate")
            count += 1
            continue
        signed = rate
        new = value + 1
        want = []
        if new <= maximum:
            want.append(("set", item, 0x48, new, 0))
            want.append(("owner-id", item))
            if owner_state != "none":
                want.append(("unit", rig.game, 0, 0x1D))
                if owner_state != "no-unit":
                    want.append(("client", rig.owner, SRC, 0x1580))
                    if owner_state != "no-client":
                        want.append(("send", 0x9000, item, 1, 0x48, new, 0))
                        if new > 0:
                            want.append(("broken", item, 0x100, 0x1586, SRC))
                            if broken:
                                want.append(("repair", rig.game, rig.owner, item))
            quotient = int(2500 / signed)           # C division truncates toward zero
            delay = max(125 if mutation != "worker-min" else 100, quotient + 1)
            want.append(("event", 0x0, item, 3, 1000 + delay, 0, 0))
        norm = [entry if entry[0] != "event" else ("event", 0x0) + entry[3:] for entry in log]
        wnorm = [entry if entry[0] != "event" else ("event", 0x0) + entry[3:] for entry in want]
        need(norm == wnorm, f"00562c40 {rate} {value} {maximum} {broken} {owner_state}: {norm} != {wnorm}")
        count += 1
    return count


def remove_item():
    """FUN_00563470: unlink an item by its current mode (stored, equipped, cursor)."""
    count = 0
    for mode, owner_kind, has_client in itertools.product((0, 1, 2, 3, 4), (0, 1), (0, 1)):
        for guard in (None, "no-game", "no-owner", "no-item", "no-inventory", "not-item"):
            if guard and (mode, owner_kind, has_client) != (0, 0, 1):
                continue
            rig = Rig(owner_kind=owner_kind)
            machine = rig.machine
            item = unit(machine, mode=mode)
            machine.w32(item, 1 if guard == "not-item" else 4)
            machine.w32(rig.owner + 0x60, 0 if guard == "no-inventory" else rig.inventory)
            rig.state["pages"] = {item: 5}
            rig.hook(0x628320, "set-page", 2)
            rig.hook(0x5531C0, "client", 1, 0x9000 if has_client else 0, ("ecx", "edx"))
            rig.hook(0x6287B0, "mask", 3)
            rig.hook(0x53D010, "send", 2, 0, ("ecx", "edx"))
            rig.hook(0x55DF10, "remove", 2, 0, ("ecx", "edx"))
            rig.hook(0x63C150, "slot", 2, 0x7)
            rig.hook(0x560CD0, "unequip", 3, 0, ("ecx", "edx"))
            rig.hook(0x55EEA0, "cursor", 1, 0, ("ecx", "edx"))
            rig.fatal_call(0x563470, item, ecx=0 if guard == "no-game" else rig.game,
                           edx=0 if guard == "no-owner" else rig.owner) if guard != "no-item" else rig.fatal_call(
                0x563470, 0, ecx=rig.game, edx=rig.owner)
            names = [entry[0] for entry in rig.log]
            if guard:
                need(names == [], f"00563470 {guard} {names}")
            elif mode == 0:
                want = ["set-page"]
                if owner_kind == 0 and has_client:
                    want += ["client", "mask", "send"]
                elif owner_kind == 0:
                    want += ["client"]
                want += ["remove"]
                if mutation == "remove-client":
                    want = [name for name in want if name not in ("client", "mask", "send")]
                need(names == want, f"00563470 stored owner {owner_kind} client {has_client}: {names}")
                need(rig.log[0] == ("set-page", item, 5) and rig.log[-1] == ("remove", rig.game, rig.owner, item, 0),
                     f"00563470 args {rig.log}")
            elif mode == 1:
                need(names == ["slot", "unequip"] and rig.log[0] == ("slot", rig.inventory, item)
                     and rig.log[1][1:3] == (rig.game, rig.owner) and rig.log[1][3] == 7 and rig.log[1][4] == 1,
                     f"00563470 equipped {rig.log}")
            elif mode == 4:
                need(names == ["cursor"] and rig.log[0][1:] == (rig.game, rig.owner, item), f"00563470 cursor {rig.log}")
            else:
                need(names == [], f"00563470 mode {mode} {names}")
            count += 1
    return count


def cube_evacuate():
    """FUN_00563840: before a cube leaves, move its page-3 contents into the inventory page."""
    count = 0
    rig = Rig()
    machine, state, log = rig.machine, rig.state, rig.log
    cube = unit(machine)
    machine.w32(cube + 4, 0xC1)
    cube_row = machine.alloc(0x200)
    plain_row = machine.alloc(0x200)
    machine.w32(cube_row + 0x80, 0x20786F62)
    machine.w32(plain_row + 0x80, 0x20202020)
    rows = {0xC1: cube_row, 0xC2: plain_row}
    st = {"unlink": None, "placed": 1, "pushed": 1, "prop": 0, "cell": 1, "contrib": 0, "client": 0, "target": None,
          "target_page": 1, "target_mode": 4}
    machine.hook(0x6335F0, lambda current: rows.get(current.arg(0), 0), 1)
    rig.hook(0x6287B0, "mask", 3)
    rig.hook(0x628320, "set-page", 2)
    rig.hook(0x5531C0, "client", 1, 0x9000, ("ecx", "edx"))
    rig.hook(0x53D010, "send", 2, 0, ("ecx", "edx"))
    machine.hook(0x63AD90, lambda current: (log.append(("unlink", current.arg(0), current.arg(1))), st["unlink"] or current.arg(1))[1], 2)
    rig.hook(0x621050, "area", 3, 0x70)
    rig.hook(0x63BCF0, "grid", 8)
    rig.hook(0x624690, "mode", 2)
    # FUN_00628280 stores the page byte; the walk re-reads it, so keep that effect.
    rig.hook(0x628280, "page", 2, lambda current: state["pages"].__setitem__(current.arg(0), current.arg(1)))
    rig.hook(0x55BF50, "check", 1, 0, ("ecx", "edx"))
    rig.hook(0x552F60, "unit", 1, lambda current: st["target"] or 0, ("ecx", "edx"))
    rig.hook(0x63B950, "place", 7, lambda current: st["placed"])
    rig.hook(0x63B210, "link", 3, lambda current: st["pushed"])
    rig.hook(0x55C270, "skill-add", 0, 0, ("ecx", "edx"))
    rig.hook(0x62FF70, "contributes", 2, lambda current: st["contrib"])
    rig.hook(0x55C2C0, "stats", 2, 0, ("ecx", "edx"))
    rig.hook(0x63C180, "cursor-clear", 2)
    rig.hook(0x628170, "transient", 3)
    rig.hook(0x55F590, "dirty", 1, lambda current: st["prop"], ("ecx", "edx"))
    rig.hook(0x6280D0, "flag-set", 3)
    rig.hook(0x6280A0, "flag-test", 4, 0)
    rig.hook(0x621000, "owner", 2)
    rig.hook(0x63CC70, "inv-add", 2)
    rig.hook(0x568770, "cube-event", 1, 0, ("ecx", "edx"))
    rig.hook(0x55DBC0, "requirements", 1, 0, ("ecx", "edx"))
    rig.hook(0x620870, "spot", 2)
    rig.hook(0x620BB0, "room", 1, 0x4444)
    rig.hook(0x555DA0, "ground", 3, lambda current: st.get("ground", 0), ("ecx", "edx"))
    rig.hook(0x558AA0, "drop", 4, 0, ("ecx", "edx"))
    items = [unit(machine, mode=0, ident=0x31), unit(machine, mode=0, ident=0x32), unit(machine, mode=0, ident=0x33)]
    chain(machine, items)
    state["first"] = items[0]
    # Non-cube: nothing but the class lookup.
    got, fatal = rig.fatal_call(0x563840, rig.game, unit(machine) and cube, regs={"ebx": rig.owner})
    machine.w32(cube + 4, 0xC2)
    log.clear()
    got, fatal = rig.fatal_call(0x563840, rig.game, cube, regs={"ebx": rig.owner})
    need(fatal is None and log == [], f"00563840 non-cube {fatal} {log}")
    count += 1
    machine.w32(cube + 4, 0xC9)
    got, fatal = rig.fatal_call(0x563840, rig.game, cube, regs={"ebx": rig.owner})
    need(fatal == 0x7FF, f"00563840 unknown class {fatal}")
    count += 1
    machine.w32(cube + 4, 0xC1)
    # Empty and page-filtered inventories do nothing.
    state["pages"] = {item: 1 for item in items}
    log.clear()
    rig.fatal_call(0x563840, rig.game, cube, regs={"ebx": rig.owner})
    need(log == [], f"00563840 no page-3 item {log}")
    count += 1
    # One page-3 item, every outcome of the automatic placement.
    for target_state, owner_kind, page in itertools.product(("no-unit", "wrong-mode", "placed", "place-fails", "link-fails"),
                                                          (0, 1), (0, 1, 2)):
        rig.machine.w32(rig.owner, owner_kind)
        state["pages"] = {items[0]: 0, items[1]: 3, items[2]: 0}
        target = unit(machine, mode=3 if target_state == "wrong-mode" else 4, ident=0x32)
        state["pages"][target] = page
        st.update(target=None if target_state == "no-unit" else target, placed=0 if target_state == "place-fails" else 1,
                  pushed=0 if target_state == "link-fails" else 1, contrib=1, ground=0)
        log.clear()
        got, fatal = rig.fatal_call(0x563840, rig.game, cube, regs={"ebx": rig.owner})
        need(fatal is None, f"00563840 {target_state} assert {fatal}")
        names = [entry[0] for entry in log]
        first = ["mask", "set-page", "client", "send", "unlink", "area", "grid", "mode", "page", "check", "unit"]
        if target_state in ("no-unit", "wrong-mode"):
            want = first + ["page", "spot", "room", "ground"]
        elif target_state == "place-fails":
            want = first + ["area", "place", "page", "spot", "room", "ground"]
        elif target_state == "link-fails":
            want = first + ["area", "place", "link", "page", "spot", "room", "ground"]
        else:
            want = first + ["area", "place", "link", "skill-add", "contributes", "stats"]
            if not (owner_kind == 0 and page == 1):
                want.append("cursor-clear")
            want += ["mode", "transient", "dirty", "flag-test", "owner", "inv-add"]
            if owner_kind == 0 and page == 2:
                want.append("cube-event")
            want += ["contributes", "requirements", "owner"]
        if mutation == "evacuate-page":
            want = want[:-1]
        need(names == want, f"00563840 {target_state} owner {owner_kind} page {page}: {names} != {want}")
        count += 1
    return count


def cube_replace():
    """FUN_00563fe0: replace a stored item with its record's replacement code and quantity."""
    count = 0
    for scenario in ("fatal-game", "fatal-owner", "fatal-item", "not-item", "no-row", "not-replaceable", "no-code",
                     "not-stored", "create-fails", "zero-range", "random-range", "first-fits", "second-fits", "none-fit"):
        rig = Rig()
        machine, log = rig.machine, rig.log
        item = unit(machine, mode=1 if scenario == "not-stored" else 0, ident=0x51)
        machine.w32(item, 1 if scenario == "not-item" else 4)
        machine.w32(item + 4, 0xD1)
        machine.mu.mem_write(machine.r32(item + 0x14) + 0x0C, (0).to_bytes(4, "little"))
        row = machine.alloc(0x200)
        machine.mu.mem_write(row + 0x139, bytes([0 if scenario == "not-replaceable" else 1]))
        low, high = (0, 4) if scenario == "zero-range" else (2, 7)
        machine.mu.mem_write(row + 0x13A, bytes([low]))
        machine.mu.mem_write(row + 0x13B, bytes([high]))
        machine.w32(row + 0xC8, 0x7777)
        machine.hook(0x6335F0, lambda current, row=row, scenario=scenario: 0 if scenario == "no-row" else row, 1)

        def class_of(current, scenario=scenario):
            if scenario == "no-code":
                return 0
            current.w32(current.arg(1), 0xE1)
            return 1

        machine.hook(0x633640, lambda current, class_of=class_of, log=log: (log.append(("code", current.arg(0))), class_of(current))[1], 2)
        created = unit(machine, mode=0, ident=0x99)

        def block(current, log=log):
            log.append(("block", current.mu.reg_read(UC_X86_REG_EAX), current.mu.reg_read(UC_X86_REG_EDI),
                        current.arg(0)))
            out = current.mu.reg_read(UC_X86_REG_ESI)
            current.w32(out + 0x1C, 11)
            current.w32(out + 0x20, 22)
            return 0

        machine.hook(0x55E8E0, block, 1)
        rig.hook(0x55E000, "remove", 1, 0, ("esi", "edi"))
        rig.hook(0x558D90, "create", 1, 0 if scenario == "create-fails" else created, ("ecx", "edx"))
        rig.hook(0x45C3E0, "rand", 0, 3, ("ecx", "edx"))
        rig.hook(0x627260, "stat", 4)
        rig.hook(0x628280, "page", 2)
        attempts = {"first-fits": (1,), "second-fits": (0, 1), "none-fit": (0, 0)}.get(scenario, (1,))
        seen = []

        def place(current, seen=seen, log=log, attempts=attempts):
            seen.append(1)
            log.append(("place",) + tuple(current.arg(index) for index in range(8)))
            return attempts[min(len(seen), len(attempts)) - 1]

        machine.hook(0x560200, place, 8)
        rig.hook(0x563C00, "drop", 1, 0, ("ecx", "edx"))
        game = 0 if scenario == "fatal-game" else rig.game
        owner = 0 if scenario == "fatal-owner" else rig.owner
        got, fatal = rig.fatal_call(0x563FE0, 0 if scenario == "fatal-item" else item, ecx=game, edx=owner)
        names = [entry[0] for entry in log]
        if scenario.startswith("fatal"):
            need(fatal == {"fatal-game": 0x1619, "fatal-owner": 0x161A, "fatal-item": 0x161B}[scenario],
                 f"00563fe0 {scenario} {fatal}")
        elif scenario == "not-item":
            need(fatal == 0x161C, f"00563fe0 not-item {fatal}")
        elif scenario == "no-row":
            need(fatal == 0x1622, f"00563fe0 no-row {fatal}")
        elif scenario == "not-replaceable":
            need(fatal is None and names == [], f"00563fe0 {scenario} {names}")
        elif scenario == "no-code":
            need(fatal == 0x1627 and names == ["code"], f"00563fe0 {scenario} {fatal} {names}")
        elif scenario == "not-stored":
            need(fatal is None and names == ["code"], f"00563fe0 {scenario} {names}")
        elif scenario == "create-fails":
            need(fatal == 0x162D and names == ["code", "block", "remove", "create"], f"00563fe0 {scenario} {fatal} {names}")
        else:
            quantity = ("stat", created, 0x46, 0, 0) if scenario == "zero-range" else ("stat", created, 0x46, 3 + 2, 0)
            if mutation == "replace-quantity" and scenario == "random-range":
                quantity = ("stat", created, 0x46, 3 + 3, 0)
            want = ["code", "block", "remove", "create"] + (["stat"] if scenario == "zero-range" else ["rand", "stat"])
            want += ["page", "place"] + (["place"] if scenario in ("second-fits", "none-fit") else [])
            want += ["drop"] if scenario == "none-fit" else []
            need(fatal is None and names == want, f"00563fe0 {scenario}: {names} != {want}")
            stat = next(entry for entry in log if entry[0] == "stat")
            need(stat == quantity, f"00563fe0 quantity {stat} != {quantity}")
            first = next(entry for entry in log if entry[0] == "place")
            need(first[1:] == (rig.game, rig.owner, 0x99, 11, 22, 0, 1, 0), f"00563fe0 first place {first}")
            if scenario in ("second-fits", "none-fit"):
                second = [entry for entry in log if entry[0] == "place"][1]
                need(second[1:] == (rig.game, rig.owner, 0x99, 0, 0, 1, 1, 0), f"00563fe0 second place {second}")
            if scenario == "random-range":
                rand = next(entry for entry in log if entry[0] == "rand")
                need(rand[1:] == (rig.owner + 0x20, 5), f"00563fe0 rand {rand}")
        count += 1
    return count


CHECKS = (tome_rebuild, unequip_all, cursor_pickup, belt_pickup, identify_item, cube_insert, replenish_worker,
          remove_item, cube_evacuate, cube_replace)
MUTATIONS = {"rebuild-all-pages": tome_rebuild, "unequip-kind": unequip_all,
             "pickup-page1-cursor": cursor_pickup, "belt-pickup-order": belt_pickup, "identify-order": identify_item,
             "cube-page": cube_insert, "worker-min": replenish_worker, "remove-client": remove_item,
             "evacuate-page": cube_evacuate, "replace-quantity": cube_replace}


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
    print(f"ok: {total} late ItemMode scenarios over 10 functions")
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
