# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_00458750's unchecked word-at-a-time record-header reads.

    python automap_read_header.py [--exe /path/to/game.exe] [--mutation-check]

The file and chain-cut helpers are hooked over a synthetic 400-byte head and
records truncated at every four-byte boundary.  Stock game.exe clears all
eight record words, asks for each word separately, and does not test any of
the eight byte counts.  Thus a missing layer word is zero and cuts a layer-1
chain, while a present layer followed by EOF ends normally through zero next
and zero list sizes.
"""
import argparse
import hashlib
import struct
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EDX

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MUTATIONS = ("expects-one-read", "missing-layer-kept")
mutation = None


class FileRig:
    """Script FUN_00458750's file calls and record a chain cut."""

    def __init__(self, machine, record):
        """Install the file hooks for one synthetic record."""
        self.machine = machine
        self.data = bytearray(400) + bytearray(record)
        struct.pack_into("<I", self.data, 4, 400)
        self.position = 0
        self.read_sizes = []
        self.cuts = 0
        machine.hook(0x457F40, lambda current: 0x123, 0)
        machine.hook(0x4069B0, self.read, 4)
        machine.hook(0x4069F0, self.seek, 2)
        machine.hook(0x4069A0, lambda current: 0, 0)
        machine.hook(0x4586E0, self.cut, 1)

    def read(self, current):
        """FUN_004069b0: copy the bytes available and publish their count."""
        count = current.arg(0)
        destination = current.mu.reg_read(UC_X86_REG_EDX)
        available = bytes(self.data[self.position:self.position + count])
        if available:
            current.mu.mem_write(destination, available)
        current.w32(current.arg(1), len(available))
        self.position += len(available)
        self.read_sizes.append(count)
        return 1

    def seek(self, current):
        """FUN_004069f0: apply the absolute seeks used by this path."""
        self.position = current.mu.reg_read(UC_X86_REG_EDX)
        return 0

    def cut(self, current):
        """FUN_004586e0: record that the current chain was cut."""
        self.cuts += 1
        return 0


def run(exe):
    """Run every four-byte truncation boundary and return the summary."""
    cases = 0
    for length in range(0, 33, 4):
        machine = emu.Emu(str(exe))
        layer_map = machine.alloc(0x20)
        machine.w32(layer_map, 1)
        machine.w32(0x7A5164, layer_map)
        record = struct.pack("<8I", 0, 1, 3, 0, 0, 0, 0, 0)[:length]
        rig = FileRig(machine, record)
        machine.call(0x458750, limit=20000)
        expected_reads = [400] + ([32] if mutation == "expects-one-read" else [4] * 8)
        expected_cuts = int(length < (4 if mutation == "missing-layer-kept" else 8))
        if rig.read_sizes != expected_reads or rig.cuts != expected_cuts:
            raise AssertionError(
                f"length {length}: reads {rig.read_sizes}, cuts {rig.cuts}; "
                f"expected {expected_reads}, {expected_cuts}"
            )
        cases += 1
    return f"ok: {cases} FUN_00458750 truncated record headers"


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
