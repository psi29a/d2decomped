# SPDX-License-Identifier: GPL-3.0-or-later
"""Check Tristram Cain's spot note FUN_005944b0 and the take FUN_005944f0 in game.exe 1.14d.

    .venv/bin/python quest_cain_spot.py [--exe /path/to/game.exe] [--mutation-check]

FUN_005944b0 (fastcall, unit in EDX; the record of quest 4 by FUN_00543640, the
unit's position by FUN_00620870, both hooked): no record, or the record's data
+0x67 set: 0, nothing written. Else the position goes to data +0x9c / +0xa0,
+0x67 = 1, returns 1. FUN_005944f0 with data +0x70 clear: data +0x91 = 1 and
+0x52 = 1 (the rest, camp Cain's make, needs +0x70 and is not run). d2d:
CainQuest::note_spot (tests/test_cain_spot.cpp). Without --exe, $D2_MPQ_DIR/bin/game.exe.
`--mutation-check` breaks the model one defect at a time and requires each to fail.
"""
import argparse
import hashlib
import os
import sys
from pathlib import Path

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MUTATIONS = ("writes-when-noted", "never-sets-flag", "returns-zero-first", "no-record-returns-one", "take-skips-camp-due", "take-skips-passed")
mutation = None


def model_note(noted, record):
    """FUN_005944b0's result and effect on (flag +0x67, x, y) for a unit at (7, 9) and prior spot (3, 4)."""
    if not record and mutation != "no-record-returns-one":
        return 0, noted, 3, 4
    if not record:
        return 1, noted, 3, 4
    if noted and mutation != "writes-when-noted":
        return 0, noted, 3, 4
    new_flag = noted if mutation == "never-sets-flag" else 1
    return (0 if mutation == "returns-zero-first" else 1), new_flag, 7, 9


def check(machine):
    """Run both functions over every case and compare with the model; returns the case count."""
    record, data, unit = machine.alloc(0x100), machine.alloc(0x200), machine.alloc(0x40)
    machine.w32(record + 0x18, data)
    state = {"record": True}

    def find(_current):
        return record if state["record"] else 0

    def position(current):
        machine.w32(current.arg(1), 7)
        machine.w32(current.arg(1) + 4, 9)
        return 0

    machine.hook(0x543640, find, 0)
    machine.hook(0x620870, position, 2)
    cases = 0
    for has_record in (True, False):
        for noted in (0, 1):
            state["record"] = has_record
            machine.mu.mem_write(data, b"\0" * 0x200)
            machine.mu.mem_write(data + 0x67, bytes([noted]))
            machine.w32(data + 0x9c, 3)
            machine.w32(data + 0xa0, 4)
            got = machine.call(0x5944b0, edx=unit, limit=2000) & 0xff
            expect = model_note(noted, has_record)
            seen = (got, machine.read(data + 0x67, 1)[0], machine.r32(data + 0x9c), machine.r32(data + 0xa0))
            if seen != expect:
                raise AssertionError(f"FUN_005944b0 record={has_record} noted={noted}: game.exe {seen}, model {expect}")
            cases += 1
    state["record"] = True
    machine.mu.mem_write(data, b"\0" * 0x200)
    machine.call(0x5944f0, ecx=unit, limit=2000)
    flags = (machine.read(data + 0x91, 1)[0], machine.read(data + 0x52, 1)[0])
    expect = (0 if mutation == "take-skips-passed" else 1, 0 if mutation == "take-skips-camp-due" else 1)
    if flags != expect:
        raise AssertionError(f"FUN_005944f0 left +0x91, +0x52 = {flags}, model {expect}")
    return cases + 1


def main():
    """Parse the arguments, verify the exe and run (with --mutation-check, then each defect)."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=Path(os.environ.get("D2_MPQ_DIR", ".")) / "bin" / "game.exe")
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    print(f"ok: {check(emu.Emu(str(args.exe)))} Cain spot-note cases (FUN_005944b0, FUN_005944f0; callees hooked)")
    if args.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                check(emu.Emu(str(args.exe)))
            except AssertionError:
                print(f"caught: {name}")
            else:
                print(f"NOT CAUGHT: {name}")
                sys.exit(1)


if __name__ == "__main__":
    main()
