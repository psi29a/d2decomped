# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the drop roll FUN_005a6600 and its wrapper FUN_005a8000 in stock game.exe 1.14d.

    uv run python corpse_drop_tier.py --exe /path/to/game.exe [--mutation-check]

FUN_005a6600 (four stack words: the dropper's context, the dying monster unit, a
flag word, a pointer to a drop record whose +8 is the target) picks a drop-tier
word from the monster's MonStats row (by the context's tier byte +0x6d), maybe
replaces it with the row's +0x8c word after four gates, and calls FUN_0055afa0
with it. The tier byte is 0..2 here: row +0x86 + 8 * tier reaches the +0x9e
byte at tier 3. FUN_005a8000 (ECX, EDX and two stack words) builds that record
on the stack from its first stack word and calls FUN_005a6600 with the flag
word 1; its second stack word is never read (docs/research/re/bugs.md 44: Find
Item's rolled tier, passed by FUN_005d8780, is dropped).

Every callee is hooked and scripted; the MonStats array is a fake block at the
data-tables pointer 0x744304 (+0xa78 base, +0xa80 count, 0x1a8 a row). The model
is written from the disassembly; the fatal lines 0x194, 0x196 and 0x19a are checked.

`--mutation-check` breaks the model one defect at a time and requires each to fail.
"""
import argparse
import hashlib
import random
import struct
from pathlib import Path

import unicorn.x86_const as uc

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MUTATIONS = ("no-selector-branch", "wrong-word-offset", "gate-skips-one", "owner-ignored", "wrapper-reads-second",
             "flag-not-one", "tier-byte-wrong", "fatal-196-as-194")
mutation = None
ROWS, ROW_SIZE = 12, 0x1A8
# name: (address, stack words the callee pops)
CALLEES = {"find": (0x620BB0, 1), "rec": (0x61A1B0, 1), "touch": (0x6427F0, 1), "ilvl": (0x558200, 0), "select": (0x5A03A0, 0),
           "table": (0x6556E0, 1), "mode": (0x5A0180, 0), "owner": (0x58F0D0, 0), "has-owner": (0x63A690, 1),
           "owner-list": (0x625760, 2), "list-type": (0x625250, 1), "list-id": (0x6252B0, 1), "resolve": (0x552F60, 1),
           "state": (0x543520, 0), "check": (0x65C310, 3), "drop": (0x55AFA0, 8)}


class Fatal(Exception):
    """game.exe's fatal exit, carrying the error number."""


class Rig:
    """The hooked callees: each logs (name, ECX, EDX, stack words) and returns the scripted value."""

    def __init__(self, machine):
        """Hook every callee, plus the fatal-error path."""
        self.m = machine
        self.log = []
        self.script = {}
        self.error = None
        for name, (address, words) in CALLEES.items():
            machine.hook(address, self._make(name, words), words if name != "drop" else 8)
        machine.hook(0x408090, self._error, 0)
        machine.hook(0x408A60, lambda current: 0, 0)
        machine.hook(0x681E09, self._exit, 0)

    def _make(self, name, words):
        """A hook for one callee."""
        def hook(current):
            """Record the call and return the scripted value (a list is consumed in order)."""
            args = tuple(current.arg(index) for index in range(words))
            self.log.append((name, current.mu.reg_read(uc.UC_X86_REG_ECX), current.mu.reg_read(uc.UC_X86_REG_EDX), args))
            value = self.script.get(name, 0)
            return value.pop(0) if isinstance(value, list) else value
        return hook

    def _error(self, current):
        """FUN_00408090: remember the error number."""
        self.error = current.arg(0)
        return 0

    def _exit(self, current):
        """FUN_00681e09: the fatal exit."""
        raise Fatal(self.error)


def build_world(machine, rng):
    """A fake data-tables block and a MonStats array of random rows; returns (array address, rows)."""
    array = machine.alloc(ROWS * ROW_SIZE)
    rows = []
    for index in range(ROWS):
        row = {"w86": [rng.getrandbits(16) for _ in range(3)], "w88": [rng.getrandbits(16) for _ in range(3)],
               "w8a": [rng.getrandbits(16) for _ in range(3)], "w8c": [rng.choice((0, rng.getrandbits(16) | 1)) for _ in range(3)],
               "b9e": rng.choice((0, 3, 9)), "b9f": rng.getrandbits(8)}
        rows.append(row)
        base = array + index * ROW_SIZE
        for tier in range(3):
            machine.mu.mem_write(base + 0x86 + tier * 8, struct.pack("<H", row["w86"][tier]))
            machine.mu.mem_write(base + 0x88 + tier * 8, struct.pack("<H", row["w88"][tier]))
            machine.mu.mem_write(base + 0x8A + tier * 8, struct.pack("<H", row["w8a"][tier]))
            machine.mu.mem_write(base + 0x8C + tier * 8, struct.pack("<H", row["w8c"][tier]))
        machine.mu.mem_write(base + 0x9E, bytes((row["b9e"], row["b9f"])))
    block = machine.alloc(0x1000)
    machine.w32(block + 0xA78, array)
    machine.w32(block + 0xA80, ROWS)
    machine.w32(0x744304, block)
    return rows


def model(rows, tier_byte, unit_class, flag, owner_slot, params):
    """The calls and the final tier word FUN_005a6600 produces for the scripted answers in `params`."""
    row = rows[unit_class]
    tier = tier_byte if mutation != "tier-byte-wrong" else tier_byte ^ 1
    calls = ["find", "rec", "touch", "ilvl", "select"]
    if params["select"] != 0xFFFFFFFF and mutation != "no-selector-branch":
        calls.append("table")
        word = row["w8a"][tier] if params["table"] == 0 else params["table_words"][tier]
    else:
        calls.append("mode")
        if params["mode4"]:
            word = row["w88"][tier]
        else:
            calls.append("mode")
            word = row["w8a"][tier] if params["mode8"] else row["w86"][tier]
    if mutation == "wrong-word-offset" and params["select"] == 0xFFFFFFFF and not params["mode4"] and not params["mode8"]:
        word = row["w88"][tier]
    if row["b9e"] and row["w8c"][tier] and owner_slot:
        target = params["target"]
        if target:
            if params["target_kind_of"](target) != 0:
                calls.append("owner")
                resolved = params["owner"]
                calls.append("has-owner")
                if params["has_owner"]:
                    calls.append("owner-list")
                    if params["owner_list"]:
                        calls += ["list-type", "list-id", "resolve"]
                        resolved = params["resolved"]
                if resolved and mutation != "owner-ignored":
                    target = resolved
            if params["target_kind_of"](target) == 0:
                calls.append("state")
                if params["state"]:
                    calls.append("check")
                    if params["check"][0] != 1:
                        calls.append("check")
                        if params["check"][1] != 1 or mutation == "gate-skips-one":
                            calls.append("check")
                            if params["check"][2] != 1:
                                word = row["w8c"][tier]
    calls.append("drop")
    return calls, word


def stack_for(machine, record_target):
    """A drop record whose +8 is record_target; returns its address."""
    record = machine.alloc(0x20)
    machine.w32(record + 8, record_target)
    return record


def run(exe):
    """Random scripted worlds through FUN_005a6600 and FUN_005a8000; returns the summary line."""
    machine = emu.Emu(str(exe))
    rig = Rig(machine)
    rng = random.Random(0x5A6600)
    rows = build_world(machine, rng)
    cases = 0
    top = emu.STACK + emu.STACK_SIZE - 0x1000
    for case in range(1500):
        unit_class = rng.randrange(ROWS)
        context = machine.alloc(0x80)
        tier_byte = rng.randrange(3)
        machine.mu.mem_write(context + 0x6D, bytes((tier_byte,)))
        unit = machine.alloc(0x40)
        machine.w32(unit + 4, unit_class)
        target_kind = rng.choice((0, 1, 1))      # the target's first word: 0 = a unit with no owner step
        target = machine.alloc(0x40)
        machine.w32(target, target_kind)
        owner_unit = machine.alloc(0x40)         # what FUN_0058f0d0 / FUN_00552f60 hand back, first word 0
        flag = rng.getrandbits(32)
        record = stack_for(machine, target if rng.random() > 0.15 else 0)
        params = {"select": rng.choice((0xFFFFFFFF, 0xFFFFFFFF, 0, 5)), "table": rng.choice((0, 0x500)),
                  "mode4": rng.getrandbits(1), "mode8": rng.getrandbits(1), "has_owner": rng.getrandbits(1),
                  "owner_list": rng.getrandbits(1), "state": rng.choice((0, 0x600, 0x600)),
                  "check": [rng.choice((0, 1, 2)) for _ in range(3)], "owner": rng.choice((0, owner_unit)),
                  "resolved": rng.choice((0, owner_unit)), "target": machine.r32(record + 8), "target_kind": target_kind,
                  "target_kind_of": lambda unit_ptr: machine.r32(unit_ptr)}
        table = machine.alloc(0x80)
        words = [rng.getrandbits(16) for _ in range(8)]
        for index, word in enumerate(words):
            machine.mu.mem_write(table + 0x2C + 2 * index, struct.pack("<H", word))
        params["table_words"] = words
        script = {"find": 0x7000, "rec": 0x8000, "ilvl": 0x1111, "select": params["select"], "table": table if params["table"] else 0,
                  "mode": [params["mode4"], params["mode8"]], "owner": params["owner"], "has-owner": params["has_owner"],
                  "owner-list": 0x900 if params["owner_list"] else 0, "list-type": 1, "list-id": 2,
                  "resolve": params["resolved"], "state": params["state"], "check": list(params["check"])}
        rig.script, rig.log = script, []
        result = None
        try:
            machine.call(0x5A6600, context, unit, flag, record, limit=20000)
        except Fatal as error:
            result = error.args[0]
        if result is not None:
            raise AssertionError(f"case {case}: fatal {result}")
        calls = [entry[0] for entry in rig.log]
        want_calls, word = model(rows, tier_byte, unit_class, flag, record != 0 and True, params)
        if calls != want_calls:
            raise AssertionError(f"case {case}: calls {calls}\n model {want_calls}")
        drop = rig.log[-1]
        if (drop[1], drop[2]) != (context, unit) or drop[3][:2] != (machine.r32(record + 8), word) or \
                drop[3][2:] != (0, 0x1111, flag, 0, 0, 0):
            raise AssertionError(f"case {case}: drop call {drop}, model tier word {word:#x}, calls {calls}, tier {tier_byte}, params {({k: v for k, v in params.items() if k != 'target_kind_of'})}, row {rows[unit_class]}")
        cases += 1
    for code, setup in ((0x194, "null"), (0x196, "unfound"), (0x19A, "range")):
        for _ in range(3):
            context = machine.alloc(0x80)
            unit = machine.alloc(0x40)
            machine.w32(unit + 4, {"null": 0, "unfound": 1, "range": rng.choice((ROWS, ROWS + 5, 0x80000001))}[setup])
            rig.script, rig.log = {"find": 0 if setup == "unfound" else 0x7000, "rec": 0x8000}, []
            try:
                machine.call(0x5A6600, context, 0 if setup == "null" else unit, 1, stack_for(machine, 0), limit=5000)
                fatal = None
            except Fatal as error:
                fatal = error.args[0]
            if mutation == "fatal-196-as-194" and code == 0x196:
                code = 0x194
            if fatal != code:
                raise AssertionError(f"{setup}: fatal {fatal}, game.exe line {code:#x}")
    cases += 9
    # The row index -1 and ROWS - 1 edges: -1 is fatal 0x19a, the last row is fine.
    last = machine.alloc(0x40)
    machine.w32(last + 4, ROWS - 1)
    rig.script, rig.log = {"find": 0x7000, "rec": 0x8000, "select": 0xFFFFFFFF, "mode": [0, 0]}, []
    machine.call(0x5A6600, machine.alloc(0x80), last, 1, stack_for(machine, machine.alloc(0x20)), limit=20000)
    if rig.log[-1][0] != "drop":
        raise AssertionError("the last MonStats row didn't reach the drop call")
    # The wrapper: FUN_005a8000 passes (ECX, EDX, 1, &record) with record +4 = EDX, +8 = its first stack word.
    seen = []
    machine.hook(0x5A6600, lambda current: seen.append(tuple(current.arg(index) for index in range(4)) +
                                                        (current.read(current.arg(3), 0x20),)) or 0, 4)
    for _ in range(40):
        ecx, edx, first, second = (rng.getrandbits(32) for _ in range(4))
        seen.clear()
        machine.call(0x5A8000, first, second, ecx=ecx, edx=edx, limit=500)
        popped = (machine.mu.reg_read(uc.UC_X86_REG_ESP) - (top - 12)) // 4
        record = seen[0][4]
        flag_word = 1 if mutation != "flag-not-one" else 0
        got = (seen[0][0], seen[0][1], seen[0][2])
        wanted_second = second if mutation == "wrapper-reads-second" else first
        if got != (ecx, edx, flag_word) or struct.unpack("<I", record[4:8])[0] != edx or \
                struct.unpack("<I", record[8:12])[0] != wanted_second or popped != 3:
            raise AssertionError(f"FUN_005a8000: {seen[0][:3]} record {record.hex()} popped {popped}")
        cases += 1
    return f"ok: {cases} scripted drop rolls, fatal lines and the wrapper"


def main():
    """Entry point: run the checks, or every mutation and require each to fail."""
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
