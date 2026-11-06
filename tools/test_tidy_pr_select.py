# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with: python3 -m unittest tools/test_tidy_pr_select.py"""
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("tidy-pr-select.py")


class SelectionTests(unittest.TestCase):
    def test_header_selects_dependent_tu(self):
        self.check_selection(True, ["src/a.cpp"])

    def test_incomplete_dependencies_select_all_project_tus(self):
        self.check_selection(False, ["src/a.cpp", "src/b.cpp"])

    def check_selection(self, complete, expected):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "build").mkdir()
            for name in ("a.cpp", "b.cpp", "changed.hpp"):
                (root / "src").mkdir(exist_ok=True)
                (root / "src" / name).touch()
            entries = [{"directory": str(root / "build"), "file": str(root / "src" / "a.cpp"), "output": "a.o"},   # older CMake: relative
                       {"directory": str(root / "build"), "file": str(root / "src" / "b.cpp"), "output": str(root / "build" / "b.o")}]
            (root / "build/compile_commands.json").write_text(json.dumps(entries))
            (root / "changed.txt").write_text("src/changed.hpp\n")
            (root / "ninja").write_text("#!/bin/sh\n" +
                "printf 'a.o: #deps 1\\n    ../src/changed.hpp\\n'\n" +
                ("printf 'b.o: #deps 0\\n'\n" if complete else ""))
            (root / "ninja").chmod(0o755)
            env = dict(os.environ, PATH=f"{root}:{os.environ['PATH']}")
            result = subprocess.run(["python3", str(SCRIPT)], cwd=root, env=env, capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout.splitlines(), [str(root / path) for path in expected])


if __name__ == "__main__":
    unittest.main()
