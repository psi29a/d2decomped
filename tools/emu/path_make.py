# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the path builder FUN_00649d00 (PATH\\Path.cpp line 0x453) and the monster mask FUN_00648480 in stock game.exe 1.14d.

    uv run python path_make.py --exe /path/to/game.exe [--mutation-check]

FUN_00649d00 (six stack words: a0 unused, a1 link flag, x, y, the unit, a5 client
flag; `ret 0x18`) allocates the unit's 0x200-byte path, stores it at unit +0x2c,
fills the position and size fields and, by unit type, the step table, mask and
type. FUN_00463860 (the class id test against 0x26) and FUN_00648480 (the path
mask from the unit's MonStats row) run for real on a fake MonStats array at the
data-tables pointer 0x744304; the pool, the pattern, the step-table, link and
projection calls are hooked and logged.

A monster class id outside the table reaches FUN_00648480 with a null row and the
next line reads address 0xd: the emulator faults there (docs/research/re/bugs.md
58). The fatal lines 0x861 / 0x871 / 0x872 can't be reached from this entry: the
path is zeroed first, so its +0x40 is 0 and a type-1 unit's first word is 1.

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
MUTATIONS = ("position-no-half", "player-mask", "flying-after-doors", "doors-mask", "class-26-mask", "type3-steps",
             "link-skipped", "client-flag-always", "size-word", "projection-ignored")
mutation = None
ROWS, ROW_SIZE = 40, 0x1A8


class Rig:
    """The hooked callees and what they were called with."""

    def __init__(self, machine):
        """Hook the pool, the pattern, the step table, the link calls and the projection."""
        self.m = machine
        self.log = []
        machine.hook(0x40B430, self._pool, 3)
        machine.hook(0x620510, lambda current: self._note("size", current.arg(0)) or 0x31, 1)
        machine.hook(0x648580, lambda current: self._note("pattern", current.arg(0), current.arg(1)) or 0x77, 2)
        machine.hook(0x648CF0, lambda current: self._note("steps", current.arg(0), current.arg(1)) or 0, 2)
        machine.hook(0x649400, lambda current: self._note("stamp", current.arg(0)) or 0, 1)
        machine.hook(0x64C350, lambda current: self._note("link", current.arg(0), current.arg(1)) or 0, 2)
        machine.hook(0x643290, self._project, 2)

    def _note(self, *entry):
        """Log a call."""
        self.log.append(entry)

    def _pool(self, current):
        """FUN_0040b430 (EDX = size, then the file, line and flag words): a block pre-filled with 0xaa."""
        self.log.append(("pool", current.mu.reg_read(uc.UC_X86_REG_EDX), self.m.cstr(current.arg(0)), current.arg(1), current.arg(2)))
        block = self.m.alloc(0x200, zero=False)
        self.m.mu.mem_write(block, b"\xaa" * 0x200)
        return block

    def _project(self, current):
        """FUN_00643290 (two in-place words): log them and add 3 / 5 so the stored result is visible."""
        first, second = current.arg(0), current.arg(1)
        x, y = self.m.r32(first), self.m.r32(second)
        self.log.append(("project", x, y))
        self.m.w32(first, x + 3)
        self.m.w32(second, y + 5)
        return 0


def build_table(machine, rng):
    """A MonStats array at the data-tables pointer; returns the rows as dicts."""
    array = machine.alloc(ROWS * ROW_SIZE)
    rows = []
    for index in range(ROWS):
        row = {"id": 0x26 if index in (7, 11) else rng.randrange(0x30, 0x7FFF), "c": rng.getrandbits(8), "d": rng.getrandbits(8)}
        rows.append(row)
        base = array + index * ROW_SIZE
        machine.mu.mem_write(base + 2, struct.pack("<H", row["id"]))
        machine.mu.mem_write(base + 0xC, bytes((row["c"], row["d"])))
    block = machine.alloc(0x1000)
    machine.w32(block + 0xA78, array)
    machine.w32(block + 0xA80, ROWS)
    machine.w32(0x744304, block)
    return rows


def model_mask(row):
    """FUN_00648480: 0x1804 flying (+0xd bit 0x40), else 0x3401 for opendoors (+0xc bit 8), else 0x3c01."""
    if mutation == "flying-after-doors" and row["c"] & 8:
        return 0x3401
    if row["d"] & 0x40:
        return 0x1804
    if mutation == "doors-mask":
        return 0x3C01
    return 0x3401 if row["c"] & 8 else 0x3C01


def model(kind, cls, rows, x, y, link, client, unit):
    """The path record's fields (offset -> value) and the call log FUN_00649d00 should leave."""
    fields = {}
    calls = [("pool", 0x200, ".\\PATH\\Path.cpp", 0x453, 0), ("size", unit), ("pattern", unit, 0x31)]
    fields[0x44], fields[0x48] = 0x31, 0x77
    fields[0] = ((x << 16) + (0x8000 if mutation != "position-no-half" else 0)) & 0xFFFFFFFF
    fields[4] = ((y << 16) + 0x8000) & 0xFFFFFFFF
    fields[0x1D8] = x & 0xFFFF if mutation != "size-word" else 0
    fields[0x1DA] = y & 0xFFFF
    fields[0x7C], fields[0x1C], fields[0x93], fields[0x1D4] = 0x800, link, 0, 1
    fields[0x30] = unit
    if kind == 0:
        fields[0x4C], fields[0x50], fields[0x91], fields[0x92] = 0x80, 0x1C09, 0x49, 0x46
        calls.append(("steps", None, 7))
    elif kind == 1:
        fields[0x4C], fields[0x3C], fields[0x98] = 0x100, 2, 0
        fields[0x91] = 0xE
        if rows[cls]["id"] == 0x26 and mutation != "class-26-mask":
            fields[0x50] = 0x804
            fields[0x48] = 5
        else:
            fields[0x50] = model_mask(rows[cls]) if mutation != "player-mask" else 0x1C09
    elif kind == 3:
        fields[0x4C], fields[0x50] = 0, 0
        calls.append(("steps", None, 4 if mutation != "type3-steps" else 7))
    if link and mutation != "link-skipped":
        calls += [("stamp", unit), ("link", unit, link)]
    calls.append(("project", (fields[0] >> 11), (fields[4] >> 11)))
    projected = (fields[0] >> 11) + (0 if mutation == "projection-ignored" else 3), (fields[4] >> 11) + (0 if mutation == "projection-ignored" else 5)
    fields[8], fields[0xC] = projected
    fields["flag 0x34"] = 0x10 if client or mutation == "client-flag-always" else 0
    return fields, calls


def run_case(machine, rig, rows, kind, cls, x, y, link, client):
    """One FUN_00649d00 call on a fresh unit; returns (unit, path address)."""
    unit = machine.alloc(0x100)
    machine.w32(unit, kind)
    machine.w32(unit + 4, cls & 0xFFFFFFFF)
    rig.log.clear()
    machine.call(0x649D00, 0, link, x, y, unit, client, limit=20000)
    return unit


def compare(machine, rig, rows, kind, cls, x, y, link, client):
    """Run one scenario and compare the path record and the call log with the model."""
    unit = run_case(machine, rig, rows, kind, cls, x, y, link, client)
    path = machine.r32(unit + 0x2C)
    fields, calls = model(kind, cls, rows, x, y, link, client, unit)
    if machine.r32(path + 0x30) != unit:
        raise AssertionError("the path doesn't point back at the unit")
    for key, value in fields.items():
        if isinstance(key, str):
            continue
        width = 2 if key in (0x1D8, 0x1DA) else 1 if key in (0x91, 0x92, 0x93) else 4
        got = int.from_bytes(machine.read(path + key, width), "little")
        if got != value & ((1 << (8 * width)) - 1):
            raise AssertionError(f"type {kind} class {cls} path +{key:#x}: {got:#x}, model {value:#x}")
    flag = machine.r32(path + 0x34) & 0x10
    if flag != fields["flag 0x34"]:
        raise AssertionError(f"client flag {flag:#x}, model {fields['flag 0x34']:#x}")
    log = []
    for entry in rig.log:
        log.append(entry)
    want = calls
    if [item[0] for item in log] != [item[0] for item in want]:
        raise AssertionError(f"type {kind}: calls {[i[0] for i in log]}, model {[i[0] for i in want]}")
    for have, need in zip(log, want):
        for index, expect in enumerate(need[1:], start=1):
            if expect is None:
                continue
            if have[index] != expect:
                raise AssertionError(f"{have[0]} word {index}: {have[index]:#x}, model {expect:#x}")
    zero_rest = bytes(machine.read(path + 0x1DC, 0x200 - 0x1DC))
    if any(byte == 0xAA for byte in zero_rest) or machine.read(path + 0x54, 0x20).count(0xAA):
        raise AssertionError("the path block wasn't zeroed")
    return path


def run(exe):
    """Random units through every type and class; returns the summary line."""
    machine = emu.Emu(str(exe))
    rig = Rig(machine)
    rng = random.Random(0x649D00)
    rows = build_table(machine, rng)
    cases = 0
    for kind in (0, 1, 3, 2, 4, 5):
        for _ in range(60 if kind != 1 else 0):
            compare(machine, rig, rows, kind, 0, rng.randrange(0, 4000), rng.randrange(0, 4000), rng.choice((0, 1, 5)),
                    rng.choice((0, 1, 7)))
            cases += 1
    for cls in range(ROWS):
        for _ in range(3):
            compare(machine, rig, rows, 1, cls, rng.randrange(0, 4000), rng.randrange(0, 4000), rng.choice((0, 1)),
                    rng.choice((0, 1)))
            cases += 1
    for cls in (ROWS, ROWS + 50, 0xFFFFFFFF, 0x80000000):
        try:
            run_case(machine, rig, rows, 1, cls, 5, 5, 0, 0)
        except RuntimeError as error:
            if "00648" not in str(error) and "0064" not in str(error):
                raise AssertionError(f"class {cls:#x} faulted elsewhere: {error}") from None
            cases += 1
            continue
        raise AssertionError(f"class {cls:#x} outside the table didn't fault (bugs.md 58)")
    return f"ok: {cases} path builds over unit types 0..5 and every MonStats class, 4 out-of-table faults"


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
