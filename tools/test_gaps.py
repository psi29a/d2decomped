# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with: python3 -m unittest tools/test_gaps.py"""
import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("gaps", Path(__file__).with_name("gaps.py"))
gaps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gaps)


class GapsTests(unittest.TestCase):
    def test_addresses_in_every_spelling(self):
        text = "FUN_005005b0, LAB_00589aa0, 0x4b35b0, 0x0045c3e0, 00675360; not 0x10, 12345678 or 0x6e55a01"
        self.assertEqual(gaps.addresses(text), [0x5005b0, 0x589aa0, 0x4b35b0, 0x45c3e0, 0x675360])

    def test_holding_maps_an_address_inside_a_body(self):
        funcs = gaps.Functions([(0x401000, "FUN_00401000", 0x20), (0x401040, "FUN_00401040", 0x10)])
        self.assertEqual(funcs.holding(0x401000), 0x401000)
        self.assertEqual(funcs.holding(0x40101f), 0x401000)
        self.assertIsNone(funcs.holding(0x401020))  # the gap between them
        self.assertIsNone(funcs.holding(0x400fff))

    def test_level_by_path(self):
        self.assertEqual(gaps.level("tools/emu/drlg.py"), 3)
        self.assertEqual(gaps.level("components/game/ai.hpp"), 2)
        self.assertEqual(gaps.level("apps/whisper/hooks.cpp"), 1)
        self.assertEqual(gaps.level("docs/research/re/drlg.md"), 1)

    def test_tables_skip_headers_and_stop_at_a_heading(self):
        lines = ["# Title", "| What | Where |", "|---|---|", "| a | FUN_00401000 |", "",
                 "## Traced", "| What |", "|---|", "| b |"]
        rows = list(gaps.tables(lines, stop="## Traced"))
        self.assertEqual(rows, [(4, "Title", {"What": "a", "Where": "FUN_00401000"})])

    def test_rows_after_prose_keep_their_header(self):
        lines = ["| Address | What |", "|---|---|", "| `0x5fd200` | a |", "", "A note.", "| `0x543de0` | b |"]
        rows = [row for _, _, row in gaps.tables(lines)]
        self.assertEqual(rows, [{"Address": "`0x5fd200`", "What": "a"}, {"Address": "`0x543de0`", "What": "b"}])

    def test_done_only_when_nothing_remains(self):
        self.assertTrue(gaps.done_line(dict.fromkeys(("gaps", "frontier", "untouched", "cited"), 0)).startswith("DONE"))
        self.assertEqual(gaps.done_line({"gaps": 2, "frontier": 0, "untouched": 0, "cited": 1}),
                         "remaining: 2 gaps, 1 cited")


if __name__ == "__main__":
    unittest.main()
