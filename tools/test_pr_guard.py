# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with: python3 -m unittest tools/test_pr_guard.py"""
import importlib.util
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("pr_guard", Path(__file__).with_name("pr_guard.py"))
pr_guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pr_guard)


class GuardTests(unittest.TestCase):
    def run_guard(self, files, labels=()):
        """Commit an empty base, then `files` on top; return (fails, warns)."""
        with tempfile.TemporaryDirectory() as directory:
            def git(*args):
                subprocess.run(["git", "-C", directory, *args], check=True, capture_output=True)
            git("init", "-q", "-b", "main")
            git("-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "--allow-empty", "-m", "base")
            for name, data in files.items():
                path = Path(directory, name)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            git("add", "-A")
            git("-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-m", "change")
            cwd = os.getcwd()
            os.chdir(directory)
            try:
                fails, warns, _ = pr_guard.check("main~1", set(labels), {0x45c3e0: 0x40})
            finally:
                os.chdir(cwd)
            return fails, warns

    def test_true_evidence_passes(self):
        fails, warns = self.run_guard({"docs/a.md": b"FUN_0045c3e0 and the table at 0x731d00\n"})
        self.assertEqual((fails, warns), ([], []))

    def test_impossible_addresses_fail(self):
        fails, _ = self.run_guard({"docs/a.md": b"FUN_00731d00 (in .data) and 0x6cbf00 (no section)\n"})
        self.assertEqual(len(fails), 2)

    def test_unknown_entry_warns(self):
        fails, warns = self.run_guard({"docs/a.md": b"FUN_00401234\n"})
        self.assertEqual((len(fails), len(warns)), (0, 1))

    def test_a_function_inside_another_body_fails(self):
        fails, warns = self.run_guard({"docs/a.md": b"FUN_0045c3f0, but LAB_0045c3f0 is fine\n"})
        self.assertEqual((len(fails), len(warns)), (1, 0))
        self.assertIn("inside FUN_0045c3e0", fails[0])

    def test_binary_outside_assets_fails(self):
        fails, _ = self.run_guard({"components/x.bin": b"\x00\x01\x02MZ", "assets/icon/i.png": b"\x89PNG\x00"})
        self.assertEqual(len(fails), 1)
        self.assertIn("components/x.bin", fails[0])

    def test_local_only_files_fail_even_as_text(self):
        fails, _ = self.run_guard({"docs/research/re/game-strings.tsv": b"address\tlength\tvalue\n",
                                   "docs/research/re/drlg.c": b"int x;\n"})
        self.assertEqual(len(fails), 2)

    def test_an_http_client_with_a_variable_url_needs_the_label(self):
        fails, _ = self.run_guard({"tools/fetch.py": b'requests.get(os.environ["SOURCE_URL"])\n'})
        self.assertEqual(len(fails), 1)

    def test_risky_code_needs_the_label(self):
        files = {"components/x.cpp": b'auto h = InternetOpen("x");\n', ".github/workflows/w.yml": b"on: push\n"}
        fails, _ = self.run_guard(files)
        self.assertEqual(len(fails), 2)
        fails, _ = self.run_guard(files, labels=["security-reviewed"])
        self.assertEqual(fails, [])


if __name__ == "__main__":
    unittest.main()
