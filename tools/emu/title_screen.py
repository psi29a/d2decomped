# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_004336c0's title records and version text in game.exe 1.14d.

    python title_screen.py [--exe /path/to/game.exe] [--mutation-check]

All callees are hooked.  The stock function must choose classic records
9..0xc or expansion records 0x10..0x13, add shared records 0xd..0xf and
text record 0x115, format `v 1.14d`, and assign it to 0x115.  The oracle also
reads 0x115's static record: (0,599), 200x40, Font16 pair, alignment 2.
"""
import argparse
import hashlib
import struct
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MUTATIONS = ("classic-uses-lod", "no-save-selects", "wrong-version", "wrong-alignment")
mutation = None
RECORD_BASE = 0x708D10


def run(exe):
    """Run both title variants and check record 0x115's static fields."""
    machine = emu.Emu(str(exe))
    config = machine.alloc(0x400)
    machine.w32(0x7795D4, config)
    records = []
    assigned = []
    expansion = [0]

    machine.stub(0, {0x43B9A0: 0, 0x43C4F0: 0, 0x431AF0: 0, 0x4F96F0: 0, 0x5148F0: 0})
    machine.hook(0x408F20, lambda current: expansion[0], 0)
    machine.hook(0x4FAC90, lambda current: 0, 0)
    machine.hook(0x514D80, lambda current: 0, 0)

    def add_record(current):
        """FUN_0042f430: log the requested record and return a stable handle."""
        index = current.mu.reg_read(UC_X86_REG_ECX)
        records.append(index)
        return 0x100000 + index

    def format_version(current):
        """FUN_00413a40: check the format pointer and its three arguments."""
        text = "v 1.14c" if mutation == "wrong-version" else "v 1.14d"
        if current.arg(2) != 0x6D48D4 or tuple(current.arg(index) for index in range(3, 6)) != (1, 0xE, 0x64):
            raise AssertionError("unexpected version format call")
        current.mu.mem_write(current.arg(0), text.encode() + b"\0")
        return len(text)

    def assign_text(current):
        """FUN_004fcff0: log record handle, text and the zero flag."""
        assigned.append((current.mu.reg_read(UC_X86_REG_ECX), machine.cstr(current.mu.reg_read(UC_X86_REG_EDX)), current.arg(0)))
        return 0

    machine.hook(0x42F430, add_record, 0)
    machine.hook(0x413A40, format_version, 0)
    machine.hook(0x4FCFF0, assign_text, 1)

    for is_expansion in (0, 1):
        expansion[0] = is_expansion
        records.clear()
        assigned.clear()
        machine.call(0x4336C0, limit=5000)
        primary = [0x12, 0x11, 0x13, 0x10] if is_expansion else [0xA, 0xB, 9, 0xC]
        if mutation == "classic-uses-lod" and not is_expansion:
            primary = [0x12, 0x11, 0x13, 0x10]
        expected_records = primary + [0xD, 0xE, 0xF, 0x115]
        expected_text = "v 1.14d"
        if records != expected_records or assigned != [(0x100115, expected_text, 0)]:
            raise AssertionError(f"expansion {is_expansion}: records {records}, text {assigned}")

    record = machine.read(RECORD_BASE + 0x115 * 0x30, 0x30)
    if len(record) != 0x30:
        raise AssertionError(f"short record 0x115: {len(record)} bytes")
    raw = list(struct.unpack("<12I", record))
    if mutation == "wrong-alignment":
        raw[11] = 0
    expected = (4, 0, 599, 200, 40, 0x7089C4, 2)
    observed = (raw[0], raw[1], raw[2], raw[3], raw[4], raw[10], raw[11])
    if observed != expected:
        raise AssertionError(f"record 0x115: {observed}, expected {expected}")

    destinations = []
    has_saves = [0]
    machine.hook(0x430BC0, lambda current: has_saves[0], 0)
    machine.hook(0x515020, lambda current: 0, 0)
    machine.hook(0x42F480, lambda current: 0, 0)
    machine.hook(0x43B080, lambda current: (destinations.append("select"), 0)[1], 0)
    machine.hook(0x435580, lambda current: (destinations.append("create"), 0)[1], 0)
    for present in (0, 1):
        has_saves[0] = present
        destinations.clear()
        machine.call(0x435CD0, 0, limit=500)
        expected_destination = "select" if present or mutation == "no-save-selects" else "create"
        if destinations != [expected_destination]:
            raise AssertionError(f"has saves {present}: destination {destinations}")
    return "ok: FUN_004336c0 records/version and SINGLE PLAYER's empty-save destination"


def main():
    """Run the oracle and require every mutation to disagree."""
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
                continue
            raise AssertionError(f"mutation survived: {name}")
        mutation = None
        print(f"mutation check ok: {len(MUTATIONS)} defects rejected")


if __name__ == "__main__":
    main()
