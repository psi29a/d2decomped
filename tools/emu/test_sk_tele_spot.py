# SPDX-License-Identifier: GPL-3.0-or-later
"""sk_tele_spot.py's own checks. Run from tools/emu: python3 -m unittest test_sk_tele_spot.py

The case generator, the parser and the diff run with the standard library
only. The last class runs game.exe's FUN_005ca360 / FUN_00554ea0 and is
skipped without unicorn (`uv sync`) and the game data (D2_MPQ_DIR, and
D2_PATCH_INSTALLER for the 1.14d tables); the facts it asserts are the ones
tests/game/test_tele_spot.cpp asserts of d2d.
"""
import importlib.util
import os
import unittest

import sk_tele_spot as tele


class CaseTests(unittest.TestCase):
    def test_gen_is_repeatable_and_parses_back(self):
        first, second = tele.gen(200, 5), tele.gen(200, 5)
        self.assertEqual(first, second)
        self.assertNotEqual(first, tele.gen(200, 6))
        for line in first:
            cid, teleport, cx, cy, tx, ty, words = tele.parse(line)
            self.assertIn(teleport, (0, 1))
            self.assertTrue(2 * tele.ROOM <= cx < 3 * tele.ROOM and 2 * tele.ROOM <= cy < 3 * tele.ROOM)
            self.assertEqual(len(line.split()), 7 + 3 * len(words))

    def test_no_units_leaves_the_unit_bit_out_and_the_rest_alone(self):
        with_units = [tele.parse(line) for line in tele.gen(300, 2)]
        without = [tele.parse(line) for line in tele.gen(300, 2, units=False)]
        self.assertTrue(any(v & tele.UNITS for case in with_units for v in case[6].values()))
        for kept, plain in zip(with_units, without, strict=True):
            self.assertEqual(kept[:6], plain[:6])
            self.assertFalse(any(v & tele.UNITS for v in plain[6].values()))
            self.assertEqual({at: v & ~tele.UNITS for at, v in kept[6].items() if v & ~tele.UNITS}, plain[6])

    def test_show_prints_a_refusal_with_dashes(self):
        self.assertEqual(tele.show(3, 0, None), "3 0 - -")
        self.assertEqual(tele.show(4, 1, (7, 9)), "4 1 7 9")

    def test_diff_names_each_kind_and_the_class_of_case(self):
        far = "0 1 100 100 190 100 0"                          # two rooms east: not near
        blocked = "1 1 100 100 110 100 1 110 100 1"
        game, d2d = ["0 0 - -", "1 1 111 100", "2 1 5 5"], ["a log line", "0 1 190 100", "1 0 - -", "2 1 5 5"]
        kinds = tele.diff(game, d2d, [far, blocked, "2 1 100 100 105 105 0"])
        self.assertEqual(kinds, {"game refuses, d2d lands (target room not near)": 1,
                                 "game lands, d2d refuses (target blocked)": 1, "same": 1})

    def test_diff_rejects_unequal_or_empty_answer_files(self):
        with self.assertRaises(ValueError):
            tele.diff(["0 1 5 5", "1 1 6 6"], ["0 1 5 5"])
        with self.assertRaises(ValueError):
            tele.diff([], [])


@unittest.skipUnless(importlib.util.find_spec("unicorn") and os.environ.get("D2_MPQ_DIR"), "needs unicorn and D2_MPQ_DIR")
class GameTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.oracle = tele.Oracle()

    def run_case(self, target, words=None, level=1):
        return self.oracle.run(level, 100, 100, *target, words or {})

    def test_a_free_target_in_a_near_room_is_where_it_lands(self):
        self.assertEqual(self.run_case((120, 100)), (1, (120, 100)))

    def test_a_target_two_rooms_away_is_refused(self):
        self.assertEqual(self.run_case((199, 100))[0], 0)

    def test_teleport_0_is_refused(self):
        self.assertEqual(self.run_case((110, 100), level=0)[0], 0)

    def test_a_blocked_target_lands_on_the_nearest_free_spot(self):
        ret, pos = self.run_case((110, 100), {(110, 100): 1})
        self.assertEqual(ret, 1)
        self.assertNotEqual(pos, (110, 100))
        self.assertLessEqual(max(abs(pos[0] - 110), abs(pos[1] - 100)), 3)


if __name__ == "__main__":
    unittest.main()
