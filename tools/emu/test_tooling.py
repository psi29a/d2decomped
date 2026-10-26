# SPDX-License-Identifier: GPL-3.0-or-later
"""Failure-path checks for the emulator's file cache and act sweep.

Run from tools/emu with: uv run python -m unittest test_tooling
"""
import os
import subprocess
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from mpq_cache import MpqCache


class CacheTests(unittest.TestCase):
    def test_mpq_cat_reports_missing_file_distinctly(self):
        binary = Path(__file__).resolve().parents[2] / "build/tools/mpq-cat/mpq-cat"
        if not binary.exists():
            self.skipTest("build mpq-cat first")
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([str(binary), directory, "absent.txt"], capture_output=True)
            self.assertEqual(result.returncode, 3, result.stderr)

    def test_changed_patch_uses_another_cache_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            patch_file = Path(directory) / "patch.exe"
            patch_file.write_bytes(b"first")
            before = MpqCache(directory, "mpq-cat", patch_file).directory
            patch_file.write_bytes(b"second version")
            after = MpqCache(directory, "mpq-cat", patch_file).directory
            self.assertNotEqual(before, after)

    def test_tool_failure_is_not_cached_and_recovers(self):
        with tempfile.TemporaryDirectory() as directory:
            oracle = MpqCache(directory, Path(directory) / "mpq-cat", Path(directory) / "patch.exe")
            oracle.directory = Path(directory) / "cache"
            failure = subprocess.CompletedProcess([], 1, b"", b"patch unavailable")
            success = subprocess.CompletedProcess([], 0, b"table", b"")
            with patch("mpq_cache.subprocess.run", side_effect=[failure, success]) as run:
                with self.assertRaisesRegex(RuntimeError, "patch unavailable"):
                    oracle.read(r"data\global\excel\monstats.txt")
                self.assertEqual(oracle.read(r"data\global\excel\monstats.txt"), b"table")
                self.assertEqual(run.call_count, 2)

    def test_confirmed_missing_file_is_cached(self):
        with tempfile.TemporaryDirectory() as directory:
            oracle = MpqCache(directory, Path(directory) / "mpq-cat", Path(directory) / "patch.exe")
            oracle.directory = Path(directory) / "cache"
            missing = subprocess.CompletedProcess([], 3, b"", b"file not found")
            with patch("mpq_cache.subprocess.run", return_value=missing) as run:
                self.assertIsNone(oracle.read("absent.txt"))
                oracle.files.clear()
                self.assertIsNone(oracle.read("absent.txt"))
                self.assertEqual(run.call_count, 1)


class SweepTests(unittest.TestCase):
    UV = """#!/bin/sh
if [ "$1" = sync ]; then exit 0; fi
level=$5
if [ "$MODE" = hang ]; then echo $$ > "$PIDS/$level"; exec sleep 60; fi
if [ "$MODE" = error ] && [ "$level" = 110 ]; then echo failed >&2; exit 7; fi
if [ "$MODE" = missing ] && [ "$level" = 110 ]; then exit 0; fi
if [ "$MODE" = mismatch ] && [ "$level" = 110 ]; then score=0; else score=1; fi
echo "monsters level $level: $score/1 seeds match game.exe"
"""

    def fake_tree(self, root, mode):
        """sweep.sh in a tree whose cmake and uv are stubs; returns it and its env."""
        emu_dir = root / "tools/emu"
        emu_dir.mkdir(parents=True)
        sweep = emu_dir / "sweep.sh"
        sweep.write_bytes(Path(__file__).with_name("sweep.sh").read_bytes())
        sweep.chmod(0o755)
        build = root / "build-release"
        build.mkdir()
        (build / "build.ninja").touch()
        bin_dir = root / "bin"
        bin_dir.mkdir()
        (bin_dir / "cmake").write_text("#!/bin/sh\nexit 0\n")
        (bin_dir / "uv").write_text(self.UV)
        for executable in bin_dir.iterdir():
            executable.chmod(0o755)
        (root / "pids").mkdir()
        env = dict(os.environ, PATH=f"{bin_dir}:{os.environ['PATH']}", ACT="5", JOBS="2", MODE=mode, PIDS=str(root / "pids"))
        return sweep, env

    def test_failure_and_missing_result_fail_the_sweep(self):
        for mode in ("ok", "mismatch", "error", "missing"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                sweep, env = self.fake_tree(Path(directory), mode)
                result = subprocess.run([str(sweep), "1-1", "monsters"], env=env, capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, mode == "ok", result.stdout + result.stderr)

    def test_term_stops_the_jobs_and_the_sweep(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sweep, env = self.fake_tree(root, "hang")
            proc = subprocess.Popen([str(sweep), "1-1", "monsters"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                for _ in range(100):
                    if len(list((root / "pids").iterdir())) == 2:
                        break
                    time.sleep(0.05)
                jobs = [int(f.read_text()) for f in (root / "pids").iterdir()]
                self.assertEqual(len(jobs), 2)
                proc.terminate()
                self.assertEqual(proc.wait(timeout=10), 143)
                time.sleep(0.2)
                for pid in jobs:
                    with self.assertRaises(ProcessLookupError, msg=f"job {pid} still runs"):
                        os.kill(pid, 0)
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
                for f in (root / "pids").iterdir():
                    try:
                        os.kill(int(f.read_text()), 9)
                    except ProcessLookupError:
                        pass

if __name__ == "__main__":
    unittest.main()
