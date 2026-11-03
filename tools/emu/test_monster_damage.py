# SPDX-License-Identifier: GPL-3.0-or-later
"""monster_damage.py's own checks. Run from tools/emu: python3 -m unittest test_monster_damage.py

The case generator, the parser and the compare run with the standard library
only. The last class runs game.exe's FUN_0057b420 / FUN_0057c1e0 and is
skipped without unicorn (`uv sync`) and the game data (D2_MPQ_DIR); the facts
it asserts are the ones tests/test_combat.cpp asserts of d2d's monster_blow.
"""
import importlib.util
import os
import unittest

import monster_damage as damage


class CaseTests(unittest.TestCase):
    def test_gen_is_repeatable_and_ids_run_from_zero(self):
        first = damage.gen(40, 7)
        self.assertEqual(first, damage.gen(40, 7))
        self.assertNotEqual(first, damage.gen(40, 8))
        self.assertEqual([int(line.split()[0]) for line in first], list(range(len(first))))

    def test_every_class_has_each_set_and_the_exact_frames_follow(self):
        count = 40
        cases = [damage.parse(line) for line in damage.gen(count, 3)]
        for name, low, high in damage.CLASSES:
            kinds = {}
            for _, kind, cls, _, _, _, _, _, ranges in cases:
                if cls == name:
                    kinds[kind] = kinds.get(kind, 0) + 1
                    self.assertTrue(all(pair == (low, high) for pair in ranges))
            self.assertEqual((kinds["roll"], kinds["reduce"], kinds["absorb"], kinds["both"]), (count, count, count, count // 4))
            self.assertEqual("negpct" in kinds, name == "andariel")
        exact = [case for case in cases if case[1] == "exact"]
        self.assertEqual(len(exact), 6 * (count // 4))
        self.assertTrue(all(case[8][0][0] == case[8][0][1] for case in exact))
        self.assertEqual({case[1] for case in cases[:-len(exact)]}, {"roll", "reduce", "absorb", "negpct", "both"})

    def test_a_line_parses_back_with_its_blows(self):
        line = damage.frame_line(5, ("absorb", "zombie1", 1, 3, 700, 0, 0, 2, 11, 666))
        self.assertEqual(line, "5 absorb zombie1 700 0 0 2 11 666 1 3 1 3 0 0")
        self.assertEqual(damage.parse(line), (5, "absorb", "zombie1", 700, 0, 0, 11, 666, [(1, 3), (1, 3)]))

    def test_seeds_and_values_are_in_range(self):
        for line in damage.gen(30, 9):
            _, _, _, pool, pct, flat, seed_low, seed_high, _ = damage.parse(line)
            self.assertTrue(0 <= seed_low < 1 << 32 and 0 <= seed_high < 1 << 32)
            self.assertTrue(pool >= 0 and flat >= 0 and -100 < pct <= 100)

    def test_compare_counts_each_difference_by_set_and_class(self):
        cases = ["0 roll fallen1 0 0 0 1 1 666 1 2 0 0 0 0", "1 roll fallen1 0 0 0 1 2 666 1 2 0 0 0 0",
                 "2 both zombie1 100 5 1 1 3 666 1 3 0 0 0 0"]
        game = ["0 300 0 5 666", "1 280 0 6 666", "2 100 0 7 666"]
        d2d = ["0 300 0 5 666", "1 256 0 6 666", "2 100 9 8 666"]
        sets = damage.compare(cases, game, d2d)
        self.assertEqual(sets[("roll", "fallen1")], [2, 1, 0, 0])
        self.assertEqual(sets[("both", "zombie1")], [1, 0, 1, 1])


@unittest.skipUnless(importlib.util.find_spec("unicorn") and os.environ.get("D2_MPQ_DIR"), "needs unicorn and D2_MPQ_DIR")
class GameTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.oracle = damage.Oracle()

    def answer(self, kind, low, high, seed_low, pool=0, pct=0, flat=0):
        line = damage.frame_line(0, (kind, "test", low, high, pool, pct, flat, 1, seed_low, 666))
        return self.oracle.run(damage.parse(line))

    def test_a_roll_is_in_256ths_and_never_reaches_the_top(self):
        seen = {self.answer("roll", 1, 2, seed)[0] for seed in range(1, 400, 7)}
        self.assertTrue(all(256 <= life < 512 for life in seen))
        self.assertGreater(len(seen), 20)

    def test_an_empty_range_rolls_and_draws_once(self):
        life, _, seed_low, _ = self.answer("roll", 0, 0, 12345)
        self.assertTrue(256 <= life < 512)
        self.assertNotEqual(seed_low, 12345)

    def test_an_exact_record_has_no_roll_and_leaves_the_seed(self):
        life, _, seed_low, seed_high = self.answer("exact", 3, 3, 777)
        self.assertEqual((life, seed_low, seed_high), (3 * 256, 777, 666))

    def test_the_flat_comes_off_before_the_percent(self):
        life, _, _, _ = self.answer("exact", 10, 10, 1, pct=20, flat=3)
        self.assertEqual(life, (10 * 256 - 3 * 256) * 80 // 100)

    def test_the_pool_takes_from_the_unreduced_record_first(self):
        life, pool, _, _ = self.answer("exact", 10, 10, 1, pool=4 * 256, pct=50, flat=0)
        self.assertEqual((life, pool), ((10 * 256 - 4 * 256) // 2, 0))


if __name__ == "__main__":
    unittest.main()
