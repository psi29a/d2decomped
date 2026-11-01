# SPDX-License-Identifier: GPL-3.0-or-later
"""MPQ setup must not be required by explicit-executable, file-free checks.

Run with: uv run python -m unittest test_emu_cache.py
Uses synthetic PE metadata and a mocked machine; no game files are needed.
"""
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

try:
    import emu
except ModuleNotFoundError as error:
    if error.name not in {"pefile", "unicorn"}:
        raise
    emu = None


@unittest.skipIf(emu is None, "install tools/emu dependencies with uv sync")
class LazyMpqTests(unittest.TestCase):
    def setUp(self):
        fake_pe = SimpleNamespace(
            OPTIONAL_HEADER=SimpleNamespace(ImageBase=0x400000, SizeOfImage=0x1000),
            DIRECTORY_ENTRY_IMPORT=[],
            get_memory_mapped_image=lambda: bytes(0x1000),
        )
        parsed = patch.object(emu.pefile, "PE", return_value=fake_pe)
        machine = patch.object(emu, "Uc")
        paths = patch.object(emu, "data_path", side_effect=SystemExit("no MPQ configuration"))
        self.addCleanup(parsed.stop)
        self.addCleanup(machine.stop)
        self.addCleanup(paths.stop)
        parsed.start()
        machine.start()
        self.data_path = paths.start()

    def test_explicit_executable_does_not_require_mpq_configuration(self):
        with patch.object(emu, "mpq_cache") as factory:
            oracle = emu.Emu(exe="synthetic.exe")
        factory.assert_not_called()
        self.data_path.assert_not_called()
        self.assertIsNone(oracle._mpq_cache)

    def test_first_read_initializes_cache_and_later_reads_reuse_it(self):
        cache = Mock()
        cache.read.side_effect = [b"first", None, b"second"]
        with patch.object(emu, "mpq_cache", return_value=cache) as factory:
            oracle = emu.Emu(exe="synthetic.exe")
            factory.assert_not_called()
            self.assertEqual(oracle.mpq_file("first.txt"), b"first")
            self.assertIsNone(oracle.mpq_file("absent.txt"))
            self.assertEqual(oracle.mpq_file("second.txt"), b"second")
        factory.assert_called_once_with()
        self.assertEqual(cache.read.call_count, 3)

    def test_missing_configuration_fails_on_first_read_instead_of_startup(self):
        with patch.object(emu, "_mpq_cache", None):
            oracle = emu.Emu(exe="synthetic.exe")
            with self.assertRaisesRegex(SystemExit, "no MPQ configuration"):
                oracle.mpq_file("first.txt")
            self.assertIsNone(oracle._mpq_cache)

    def test_failed_setup_can_be_retried(self):
        cache = Mock()
        cache.read.return_value = b"recovered"
        with patch.object(emu, "mpq_cache", side_effect=[RuntimeError("setup failed"), cache]) as factory:
            oracle = emu.Emu(exe="synthetic.exe")
            factory.assert_not_called()
            with self.assertRaisesRegex(RuntimeError, "setup failed"):
                oracle.mpq_file("first.txt")
            self.assertIsNone(oracle._mpq_cache)
            self.assertEqual(oracle.mpq_file("first.txt"), b"recovered")
        self.assertEqual(factory.call_count, 2)


if __name__ == "__main__":
    unittest.main()
