# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the three magic and two rare affix table merges in stock game.exe.

Usage: D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_affixes.py

CompileTxt and string lookup are hooked. The original merge, copies, segment
pointer assignments, name-ID writes, and input-table releases run locally.
No MPQ is read.
"""

import random
import struct

from _monster_world import require
from monster_loaders import COMPILE, Rig


SPECS = (
    (0x633730, ("magicsuffix", "magicprefix", "automagic"), 0x90,
     0x96CA7C, 0x96CA80, (0x96CA84, 0x96CA88, 0x96CA8C), 0x20, 0x297),
    (0x633F40, ("raresuffix", "rareprefix"), 0x48,
     0x96CAA0, 0x96CAA4, (0x96CAA8, 0x96CAAC), 0x0C, 0x2F4),
)
FILE = ".\\DATATBLS\\ItemTbls.cpp"


def check(spec, counts, seed):
    """Compare merged bytes, segment pointers, IDs, calls and frees."""
    (function, names, stride, count_slot, output_slot, segment_slots,
     string_id_offset, allocation_line) = spec
    rng = random.Random(seed)
    rig = Rig()
    inputs = {}
    records = {}
    for name, count in zip(names, counts):
        data = bytearray(rng.randbytes(max(stride, count * stride)))
        for row in range(count):
            struct.pack_into("<H", data, row * stride + string_id_offset, 0)
        pointer = rig.m.alloc(len(data))
        rig.write(pointer, data)
        inputs[name] = (pointer, count)
        records[name] = data
    rig.ids = [0x100 + index for index in range(sum(counts))]

    def compile_table(current):
        """Feed each CompileTxt call its own synthetic table and row count."""
        name = current.cstr(current.arg(1))
        rig.table, rig.rows = inputs[name]
        return rig.compile(current)

    rig.m.hook(COMPILE, compile_table, 5)
    result = rig.go(function, ecx=0)
    require(result[0] == "ret", f"affix loader {function:#x}: {result}; {rig.log}")
    total = sum(counts)
    output = rig.m.r32(output_slot)
    require(rig.m.r32(count_slot) == total and output != 0,
            f"affix count/array {function:#x}")
    offsets = [0]
    for count in counts[:-1]:
        offsets.append(offsets[-1] + count * stride)
    observed_segments = tuple(rig.m.r32(slot) for slot in segment_slots)
    require(observed_segments == tuple(output + offset for offset in offsets),
            f"affix segment pointers {observed_segments}")
    expected = bytearray().join(records[name][:count * stride] for name, count in zip(names, counts))
    for index in range(total):
        struct.pack_into("<H", expected, index * stride + string_id_offset, 0x100 + index)
    require(rig.m.read(output, total * stride) == expected,
            f"affix merged rows differ for {function:#x}, counts {counts}")
    compiles = [entry[2] for entry in rig.log if entry[0] == "compile"]
    require(compiles == list(names), f"affix compile order {compiles}")
    frees = [entry for entry in rig.log if entry[0] == "unload"]
    require(frees == [("unload", inputs[name][0]) for name in names],
            f"affix input release order {frees}")
    allocation = [entry for entry in rig.log if entry[0] == "alloc"]
    require(allocation == [("alloc", 0, total * stride, FILE, allocation_line, 0)],
            f"affix merged allocation {allocation}")
    lookups = [entry for entry in rig.log if entry[0] == "lookup"]
    expected_ptrs = [output + index * stride + (0x26 if function == 0x633F40 else 0)
                     for index in range(total)]
    require([entry[1] for entry in lookups] == expected_ptrs,
            f"affix string lookup targets {lookups}")


def main():
    """Sweep empty and varied input sizes for both merge functions."""
    count_cases = ((0, 0, 0), (1, 0, 0), (0, 2, 0), (0, 0, 3),
                   (1, 2, 3), (3, 1, 2))
    checks = 0
    for spec in SPECS:
        for index, counts in enumerate(count_cases):
            check(spec, counts[:len(spec[1])], 100 * spec[0] + index)
            checks += 1
    print(f"ok: {checks} magic and rare affix merge cases")


if __name__ == "__main__":
    main()
