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

    def test_hooked_functions_are_citations_not_executed_addresses(self):
        source = """machine.hook(0x401000, callback, 2)
machine.stub(0, {
    0x401040: 1,
    0x401080: 2,
})
machine.call(0x4010c0)
machine.hook(target, callback, 0)
"""
        self.assertEqual(gaps.hooked_address_lines("tools/emu/oracle.py", source), {1, 3, 4})
        self.assertEqual(gaps.hooked_address_lines("tests/oracle.py", source), set())

    def test_an_oracle_can_declare_exactly_which_addresses_it_runs(self):
        source = """# gaps-runs: FUN_00401000 00401040
# gaps-runs: 0x401080 FUN_00401000
machine.call(0x4010c0)
"""
        self.assertEqual(gaps.declared_oracle_runs("tools/emu/oracle.py", source),
                         [0x401000, 0x401040, 0x401080])
        self.assertIsNone(gaps.declared_oracle_runs("tools/emu/plain.py", "machine.call(0x401000)"))
        self.assertIsNone(gaps.declared_oracle_runs("tests/oracle.py", source))

    def test_tables_skip_headers_and_stop_at_a_heading(self):
        lines = ["# Title", "| What | Where |", "|---|---|", "| a | FUN_00401000 |", "",
                 "## Traced", "| What |", "|---|", "| b |"]
        rows = list(gaps.tables(lines, stop="## Traced"))
        self.assertEqual(rows, [(4, "Title", {"What": "a", "Where": "FUN_00401000"})])

    def test_rows_after_prose_keep_their_header(self):
        lines = ["| Address | What |", "|---|---|", "| `0x5fd200` | a |", "", "A note.", "| `0x543de0` | b |"]
        rows = [row for _, _, row in gaps.tables(lines)]
        self.assertEqual(rows, [{"Address": "`0x5fd200`", "What": "a"}, {"Address": "`0x543de0`", "What": "b"}])

    def test_a_note_runs_on_through_its_comment_lines(self):
        lines = ["    // ponytail: a point given twice", "    // is dropped (FUN_00650090).", "    code();",
                 "x();  // ponytail: trailing", "// next line", "// ponytail: one", "// ponytail: two", "//", "// after"]
        self.assertEqual(list(gaps.note_spans(lines)), [
            (1, 2, "a point given twice is dropped (FUN_00650090)."), (4, 4, "trailing"), (6, 6, "one"), (7, 7, "two")])

    def test_done_only_when_nothing_remains(self):
        self.assertTrue(gaps.done_line(dict.fromkeys(("gaps", "frontier", "untouched", "cited"), 0)).startswith("DONE"))
        self.assertEqual(gaps.done_line({"gaps": 2, "frontier": 0, "untouched": 0, "cited": 1}),
                         "remaining: 2 gaps, 1 cited")


    def test_needs_from_a_notes_words(self):
        def needs(text): return [n for n, p in gaps.NEEDS.items() if gaps.re.search(p, text, gaps.re.I)]
        self.assertEqual(needs("a walk home always sets off"), ["pathing"])
        self.assertEqual(needs("It reads the client's talk state"), ["client state"])
        self.assertEqual(needs("a dual-wielder's primary hand"), ["dual-wield"])
        self.assertEqual(needs("FUN_00526100's table"), [])

    def test_shade_runs_red_to_green(self):
        self.assertEqual(gaps.shade(0), "hsl(0, 70%, 42%)")      # red
        self.assertEqual(gaps.shade(50), "hsl(60, 70%, 42%)")    # yellow
        self.assertEqual(gaps.shade(100), "hsl(120, 70%, 42%)")  # green

if __name__ == "__main__":
    unittest.main()
