# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with: python3 -m unittest test_structs.py   (no unicorn needed)"""
import unittest

import structs


class Memory:
    def __init__(self): self.bytes, self.mu = bytearray(0x200), self
    def read(self, addr, n): return bytes(self.bytes[addr:addr + n])
    def mem_write(self, addr, data): self.bytes[addr:addr + len(data)] = data


class StructsTests(unittest.TestCase):
    def test_fields_land_at_their_offsets(self):
        memory = Memory()
        unit = structs.view(memory, "unit", 0x10)
        unit.type, unit["class"], unit.seed_high = 1, 60, 666
        self.assertEqual(memory.bytes[0x10:0x18], bytes([1, 0, 0, 0, 60, 0, 0, 0]))
        self.assertEqual(unit.seed_high, 666)
        game = structs.view(memory, "game", 0)
        game.difficulty = 2
        self.assertEqual(memory.bytes[0x6d], 2)
        unit.flags = -1                                     # unsigned: wraps
        self.assertEqual(unit.flags, 0xFFFFFFFF)

    def test_every_row_is_well_formed(self):
        for kind, fields in structs.FIELDS.items():
            offsets = sorted(fields.values())
            for (low, form), (high, _) in zip(offsets, offsets[1:]):
                self.assertLessEqual(low + structs.struct.calcsize(form), high, f"{kind} fields overlap at {high:#x}")

    def test_every_row_cites_a_line_that_exists(self):
        root = structs.TABLE.parents[3]
        with open(structs.TABLE, encoding="utf-8") as f:
            for row in list(f)[1:]:
                path, line = row.rstrip("\n").split("\t")[-1].rsplit(":", 1)
                with open(root / path, encoding="utf-8") as cited:
                    self.assertLessEqual(int(line), sum(1 for _ in cited), row)

    def test_an_unknown_field_is_an_error(self):
        with self.assertRaises(KeyError):
            structs.view(Memory(), "unit", 0).nope


if __name__ == "__main__":
    unittest.main()
