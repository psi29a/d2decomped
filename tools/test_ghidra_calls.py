# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with: python3 -m unittest tools/test_ghidra_calls.py"""
import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("calls", Path(__file__).parent / "ghidra" / "calls.py")
calls = importlib.util.module_from_spec(spec)
spec.loader.exec_module(calls)

GRAPH = {"root": ["a", "x"], "a": ["x"], "x": ["y", "root"], "y": ["t"]}


def edges(address):
    return [(name, name) for name in GRAPH.get(address, [])]


class CallsTests(unittest.TestCase):
    def test_depth_is_levels_below_the_start(self):
        self.assertEqual(calls.walk("root", 1, None, edges), ["a", "x"])
        self.assertEqual(calls.walk("root", 2, None, edges), ["a", "  x", "x", "  y", "  root"])

    def test_a_shorter_path_walks_a_function_again(self):
        # x is seen first by root -> a -> x, too deep to reach t; root -> x -> y -> t isn't.
        self.assertEqual(calls.walk("root", 3, "t", edges), ["root -> x -> y -> t"])


if __name__ == "__main__":
    unittest.main()
