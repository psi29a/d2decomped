# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_00438f70's save-name filter and FILETIME forwarding.

    python save_folder_scan.py [--exe /path/to/game.exe] [--mutation-check]

FindFirstFileA/FindNextFileA provide synthetic WIN32_FIND_DATAA records.
FUN_0043c8a0 and FUN_00438ad0 are hooked so the stock scanner alone decides
which names reach the header reader and which last-write low/high words reach
the list-entry builder.  No host files are opened.
"""
import argparse
import hashlib
import struct
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MUTATIONS = ("accept-short", "accept-underscore", "drop-filetime")
mutation = None


class Rig:
    """A synthetic folder around the real FUN_00438f70."""

    def __init__(self, exe, rows):
        """Install folder, string, header-reader and entry-builder hooks."""
        self.machine = emu.Emu(str(exe))
        self.rows = rows
        self.index = 0
        self.headers = []
        self.entries = []
        self.machine.hook(0x407050, self.directory, 0)
        self.machine.hook(0x6823FD, self.sprintf, 0)
        self.machine.hook(0x43C8A0, self.header, 0)
        self.machine.hook(0x438AD0, self.entry, 5)
        self.imported("FindFirstFileA", self.first, 2)
        self.imported("FindNextFileA", self.next, 2)
        self.imported("FindClose", lambda current: 1, 1)

    def imported(self, name, handler, count):
        """Replace a named imported function with a scripted handler."""
        for slot, (candidate, _) in list(self.machine.slots.items()):
            if candidate == name:
                self.machine.slots[slot] = (name, lambda current: (handler(current), count))
                return
        raise AssertionError(f"missing import {name}")

    def directory(self, current):
        """FUN_00407050: supply a harmless synthetic save directory."""
        current.mu.mem_write(current.mu.reg_read(UC_X86_REG_ECX), b"C:\\Save\\\0")
        return 0

    def sprintf(self, current):
        """The scanner's only sprintf builds `<save>*.d2s`."""
        destination = current.arg(0)
        text = self.machine.cstr(current.arg(2)) + "*.d2s"
        current.mu.mem_write(destination, text.encode() + b"\0")
        return len(text)

    def put_row(self, destination, row):
        """Write filename and last-write FILETIME into WIN32_FIND_DATAA."""
        name, low, high = row
        current = bytearray(320)
        struct.pack_into("<II", current, 20, low, high)
        current[44:44 + len(name) + 1] = name.encode() + b"\0"
        self.machine.mu.mem_write(destination, bytes(current))

    def first(self, current):
        """FindFirstFileA: publish the first row."""
        self.index = 0
        self.put_row(current.arg(1), self.rows[0])
        return 0x123

    def next(self, current):
        """FindNextFileA: publish each remaining row."""
        self.index += 1
        if self.index >= len(self.rows):
            return 0
        self.put_row(current.arg(1), self.rows[self.index])
        return 1

    def header(self, current):
        """FUN_0043c8a0: record the filtered stem and accept its header."""
        self.headers.append(self.machine.cstr(current.mu.reg_read(UC_X86_REG_ECX)))
        return 1

    def entry(self, current):
        """FUN_00438ad0: record name and the forwarded FILETIME pair."""
        self.entries.append((self.machine.cstr(current.arg(0)), current.arg(3), current.arg(4)))
        return 1


def run(exe):
    """Run boundary names and distinct FILETIMEs through the stock scanner."""
    rows = [
        ("A.d2s", 1, 10), ("Ab.d2s", 2, 20), ("123456789012345.d2s", 3, 30),
        ("1234567890123456.d2s", 4, 40), ("_Hero.d2s", 5, 50),
        ("Hero-.d2s", 6, 60), ("Hero.d2s", 0x89ABCDEF, 0x76543210),
    ]
    rig = Rig(exe, rows)
    rig.machine.call(0x438F70, limit=30000)
    expected = [
        ("Ab", 2, 20), ("123456789012345", 3, 30),
        ("Hero", 0x89ABCDEF, 0x76543210),
    ]
    if mutation == "accept-short":
        expected.insert(0, ("A", 1, 10))
    elif mutation == "accept-underscore":
        expected.insert(2, ("_Hero", 5, 50))
    elif mutation == "drop-filetime":
        expected[-1] = ("Hero", 0, 0)
    if rig.entries != expected or rig.headers != [entry[0] for entry in expected]:
        raise AssertionError(f"headers {rig.headers}, entries {rig.entries}, expected {expected}")
    return f"ok: FUN_00438f70 accepted {len(expected)} of {len(rows)} names and forwarded FILETIMEs"


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
