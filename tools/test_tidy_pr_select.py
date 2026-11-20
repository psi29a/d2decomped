# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with: python3 -m unittest tools/test_tidy_pr_select.py"""
import importlib.util
import io
import json
import os
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location("tidy_pr_select", Path(__file__).with_name("tidy-pr-select.py"))
tidy_pr_select = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tidy_pr_select)

# components/lib: core.hpp <- mid.hpp <- top.hpp; a.cpp includes core.hpp itself,
# b.cpp only top.hpp; c.cpp includes nothing of these; t.cpp includes cases.inc.
# apps/app: d.cpp reaches core.hpp through umbrella.hpp, and its .clang-tidy
# lets it (IgnoreHeaders core.hpp).
FILES = {
    "components/lib/core.hpp": "#pragma once\nint core();\n",
    "components/lib/mid.hpp": '#pragma once\n#include "core.hpp"\n',
    "components/lib/top.hpp": '#pragma once\n#include "mid.hpp"\n',
    "components/lib/a.cpp": '#include "core.hpp"\n',
    "components/lib/b.cpp": '#include "top.hpp"\n',
    "components/lib/c.cpp": "int c;\n",
    "tests/cases.inc": "1, 2,\n",
    "tests/t.cpp": '#include "cases.inc"\n',
    "apps/app/umbrella.hpp": "#pragma once\n#include <core.hpp>\n",
    "apps/app/d.cpp": '#include "umbrella.hpp"\n',
    "apps/app/.clang-tidy": "CheckOptions:\n  misc-include-cleaner.IgnoreHeaders: 'umbrella\\.hpp;core\\.hpp'\n",
}
TUS = ["components/lib/a.cpp", "components/lib/b.cpp", "components/lib/c.cpp", "tests/t.cpp", "apps/app/d.cpp"]


class SelectionTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(os.path.realpath(self.directory.name))
        for name, text in FILES.items():
            (self.root / name).parent.mkdir(parents=True, exist_ok=True)
            (self.root / name).write_text(text)
        (self.root / "build").mkdir()
        entries = [{"directory": str(self.root / "build"), "file": str(self.root / tu), "output": tu + ".o"} for tu in TUS]
        (self.root / "build/compile_commands.json").write_text(json.dumps(entries))
        tidy_pr_select._ignore_cache.clear()

    def tearDown(self):
        self.directory.cleanup()

    def run_select(self, changed, *flags):
        (self.root / "changed.txt").write_text("".join(line + "\n" for line in changed))
        stdout = io.StringIO()
        original_dir = Path.cwd()
        try:
            os.chdir(self.root)
            with (patch.object(tidy_pr_select, "ROOT", str(self.root)),
                  patch.dict(os.environ, BUILD_DIR="build"),
                  redirect_stdout(stdout)):
                self.assertEqual(tidy_pr_select.main(list(flags)), 0)
        finally:
            os.chdir(original_dir)
        return [os.path.relpath(line, self.root) for line in stdout.getvalue().splitlines()]

    def test_changed_tu_alone(self):
        self.assertEqual(self.run_select(["components/lib/c.cpp"]), ["components/lib/c.cpp"])

    def test_header_takes_its_own_includers_and_one_tu_above(self):
        # a.cpp includes core.hpp itself; b.cpp covers mid.hpp and top.hpp; d.cpp may
        # reach core.hpp through its umbrella (its .clang-tidy says so)
        self.assertEqual(self.run_select(["components/lib/core.hpp"]),
                         ["apps/app/d.cpp", "components/lib/a.cpp", "components/lib/b.cpp"])

    def test_comment_only_header_takes_one_tu(self):
        self.assertEqual(len(self.run_select(["components/lib/core.hpp\tcomment"])), 1)

    def test_transitive_takes_every_tu_that_reaches_it(self):
        self.assertEqual(self.run_select(["components/lib/core.hpp"], "--transitive"),
                         ["apps/app/d.cpp", "components/lib/a.cpp", "components/lib/b.cpp"])
        self.assertEqual(self.run_select(["components/lib/top.hpp"], "--transitive"), ["components/lib/b.cpp"])

    def test_inc_takes_every_tu_that_reaches_it(self):
        self.assertEqual(self.run_select(["tests/cases.inc"]), ["tests/t.cpp"])

    def test_deleted_file_is_skipped(self):
        self.assertEqual(self.run_select(["components/lib/gone.hpp", "components/lib/c.cpp"]), ["components/lib/c.cpp"])

    def test_file_outside_the_graph_takes_everything(self):
        (self.root / "cmake").mkdir()
        (self.root / "cmake/forced.hpp").write_text("#pragma once\n")   # -include'd by every TU: nothing names it
        self.assertEqual(sorted(self.run_select(["cmake/forced.hpp"])), sorted(TUS))

    def test_comment_only_against_a_base(self):
        git = lambda *args: subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", *args], cwd=self.root,
                                           check=True, capture_output=True)
        git("init", "-q")
        git("add", ".")
        git("commit", "-q", "-m", "base")
        header = self.root / "components/lib/core.hpp"
        header.write_text("#pragma once\n// what core() returns\nint core();\n")
        self.assertTrue(tidy_pr_select.comment_only(str(header), "HEAD", cwd=self.root))
        header.write_text("#pragma once\nint core(int);\n")
        self.assertFalse(tidy_pr_select.comment_only(str(header), "HEAD", cwd=self.root))


if __name__ == "__main__":
    unittest.main()
