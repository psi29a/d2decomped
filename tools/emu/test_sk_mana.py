# SPDX-License-Identifier: GPL-3.0-or-later
"""sk_mana.py's own checks. Run from tools/emu: python3 -m unittest test_sk_mana.py

The case grid, the arithmetic and the diff run with the standard library
only. The last class runs game.exe's FUN_0056c160 / FUN_0056bfe0 and is
skipped without unicorn (`uv sync`) and the game data (D2_MPQ_DIR, and
D2_PATCH_INSTALLER for the 1.14d tables); the facts it asserts are the ones
tests/game/test_sk_mana.cpp asserts of d2d.
"""
import importlib.util
import os
import unittest

import sk_mana

ROW = {"startmana": 0, "minmana": 1, "manashift": 0, "mana": 0, "lvlmana": 0}
TELEPORT = 54                                                    # Skills.txt row 54, Teleport (V: test_sk_mana.cpp finds it by name)


class GridTests(unittest.TestCase):
    def test_formula_is_the_shifted_price(self):
        row = {"mana": 6, "lvlmana": 2, "manashift": 1}
        self.assertEqual(sk_mana.formula(row, 1), 12)
        self.assertEqual(sk_mana.formula(row, 5), (6 + 2 * 4) << 1)

    def test_formula_has_no_minmana_floor(self):
        self.assertEqual(sk_mana.formula(ROW, 25), 0)

    def test_thresholds_surround_the_formula_and_the_floor(self):
        row = {"mana": 24 * 256, "lvlmana": 0, "manashift": 0, "minmana": 1}
        values = sk_mana.thresholds(row, 1)
        self.assertEqual(values, sorted(set(values)))
        for wanted in (24 * 256 - 1, 24 * 256, 24 * 256 + 1, 255, 256, 257, 0):
            self.assertIn(wanted, values)
        self.assertTrue(all(value >= 0 for value in sk_mana.thresholds(ROW, 25)))

    def test_diff_counts_what_differs_and_skips_no_cast_path(self):
        cases = ["0 54 25 128", "1 54 25 256", "2 7 1 0"]
        game = ["0 1 0", "1 1 256", "2 0 0"]
        d2d = ["2026-11-03T10:00:00Z [WARNING] a log line", "0 0 0", "1 1 256", "2 -1 -1"]
        total, same, kinds, by_skill = sk_mana.diff(cases, game, d2d)
        self.assertEqual((total, same), (2, 1))
        self.assertEqual(kinds, {"game casts, d2d refuses": 1})
        self.assertEqual(by_skill, {"54": 1})

    def test_diff_rejects_a_short_or_empty_d2d_file(self):
        cases, game = ["0 54 25 128", "1 54 25 256"], ["0 1 0", "1 1 256"]
        with self.assertRaises(ValueError):
            sk_mana.diff(cases, game, ["0 1 0"])
        with self.assertRaises(ValueError):
            sk_mana.diff(cases, game, ["0 -1 -1", "1 -1 -1"])

    def test_signed_reads_two_complement(self):
        self.assertEqual(sk_mana.signed(0xFFFF, 16), -1)
        self.assertEqual(sk_mana.signed(0x7FFF, 16), 0x7FFF)
        self.assertEqual(sk_mana.signed(0xFFFFFF00, 32), -256)


@unittest.skipUnless(importlib.util.find_spec("unicorn") and os.environ.get("D2_MPQ_DIR"), "needs unicorn and D2_MPQ_DIR")
class GameTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.oracle = sk_mana.Oracle()

    def test_teleport_casts_for_half_a_point_and_pays_nothing(self):
        check, paid, _ = self.oracle.run(TELEPORT, 25, 128)
        self.assertEqual((check != 0, paid), (True, 0))

    def test_teleport_pays_the_floor_from_enough_mana(self):
        for mana in (256, 300):
            check, paid, _ = self.oracle.run(TELEPORT, 25, mana)
            self.assertEqual((check != 0, paid), (True, 256), mana)

    def test_teleport_level_1_is_refused_one_short(self):
        self.assertEqual(self.oracle.run(TELEPORT, 1, 24 * 256 - 1)[0], 0)
        check, paid, _ = self.oracle.run(TELEPORT, 1, 24 * 256)
        self.assertEqual((check != 0, paid), (True, 24 * 256))

    def test_gen_covers_every_row_and_is_repeatable(self):
        first, second = sk_mana.gen(50, 1, self.oracle), sk_mana.gen(50, 1, self.oracle)
        self.assertEqual(first, second)
        self.assertGreater(len(first), self.oracle.count * 60)


if __name__ == "__main__":
    unittest.main()
