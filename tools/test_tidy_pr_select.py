# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with: python3 -m unittest tools/test_tidy_pr_select.py"""
import importlib.util
import io
import json
import os
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location("tidy_pr_select", Path(__file__).with_name("tidy-pr-select.py"))
tidy_pr_select = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tidy_pr_select)


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
            deps = "a.o: #deps 1\n    ../src/changed.hpp\n"
            if complete:
                deps += "b.o: #deps 0\n"
            stdout = io.StringIO()
            original_dir = Path.cwd()
            try:
                os.chdir(root)
                parsed = tidy_pr_select.parse_ninja_deps("build", deps)
                with (patch.object(tidy_pr_select, "ninja_deps", return_value=parsed) as ninja,
                      patch.dict(os.environ, BUILD_DIR="build"),
                      redirect_stdout(stdout)):
                    self.assertEqual(tidy_pr_select.main(), 0)
            finally:
                os.chdir(original_dir)
            ninja.assert_called_once_with("build")
            self.assertEqual(stdout.getvalue().splitlines(), [str(root / path) for path in expected])


if __name__ == "__main__":
    unittest.main()
