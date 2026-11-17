# SPDX-License-Identifier: GPL-3.0-or-later
"""open_wounds.py compare: the rejections and the accepted records. Run from tools/emu: python3 -m unittest test_open_wounds_compare.py

Standard library only; no game data and no emulator.
"""
import contextlib
import io
import os
import tempfile
import unittest

import open_wounds

PLAYER_CASE = "0 2 0 5 0"
MONSTER_CASE = "1 2 1 5 0"
PLAYER_GAME = "0 1 -4 200 74 62"
MONSTER_GAME = "1 1 -49 200 74 62"      # 49 x 200 = 9,800 256ths


def run(cases, game, d2d):
    """-> (exit code or None, stdout, stderr text of the SystemExit)"""
    with tempfile.TemporaryDirectory() as tmp:
        paths = []
        for name, lines in (("c", cases), ("g", game), ("d", d2d)):
            path = os.path.join(tmp, name)
            with open(path, "w", encoding="utf-8") as f:
                f.write("\n".join(lines) + "\n")
            paths.append(path)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            try:
                open_wounds.compare(*paths)
            except SystemExit as e:
                return e.code, out.getvalue()
    return None, out.getvalue()


class CompareTests(unittest.TestCase):
    def test_valid_player_and_monster_rows_pass(self):
        code, out = run([PLAYER_CASE, MONSTER_CASE], [PLAYER_GAME, MONSTER_GAME], ["0 -", "1 1 9800"])
        self.assertEqual(code, 0)
        self.assertIn("1 monster cases compared, 0 differ", out)

    def test_player_row_must_be_id_dash(self):
        for bad in ("0 invalid", "0 -x", "0 - 1", "0 1", "0"):
            code, _ = run([PLAYER_CASE, MONSTER_CASE], [PLAYER_GAME, MONSTER_GAME], [bad, "1 1 9800"])
            self.assertNotEqual(code, 0, bad)
            self.assertIsInstance(code, str, bad)           # an abort with a message, not a count

    def test_player_row_message_names_the_id(self):
        code, _ = run([PLAYER_CASE, MONSTER_CASE], [PLAYER_GAME, MONSTER_GAME], ["0 invalid", "1 1 9800"])
        self.assertEqual(code, "compare: bad player row near id 0")

    def test_game_record_with_other_stat_or_state_differs(self):
        code, _ = run([MONSTER_CASE], ["1 1 -49 200 5 62"], ["1 1 9800"])
        self.assertEqual(code, 1)

    def test_non_numeric_game_field_aborts(self):
        code, _ = run([MONSTER_CASE], ["1 1 -49 200 invalid invalid"], ["1 1 9800"])
        self.assertEqual(code, "compare: non-numeric field near id 1")

    def test_no_monster_case_compared_exits_1(self):
        code, _ = run([PLAYER_CASE], [PLAYER_GAME], ["0 -"])
        self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
