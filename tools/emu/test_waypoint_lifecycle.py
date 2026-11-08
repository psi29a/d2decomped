# SPDX-License-Identifier: GPL-3.0-or-later
"""Oracle argument checks and stock-code mutation controls; skips stock without data."""
import contextlib
import importlib.util
import io
import os
import unittest

import waypoint_lifecycle as waypoint


class ArgumentTests(unittest.TestCase):
    """Malformed arguments must fail before loading local game files."""

    def test_bad_arguments(self):
        """Reject both an unknown flag and a positional argument."""
        for arguments in (["--unknown"], ["unexpected"]):
            with (self.subTest(arguments=arguments),
                  contextlib.redirect_stderr(io.StringIO())):
                with self.assertRaises(SystemExit) as caught:
                    waypoint.main(arguments)
                self.assertEqual(caught.exception.code, 2)


@unittest.skipUnless(
    importlib.util.find_spec("unicorn") and os.environ.get("D2_MPQ_DIR"),
    "needs unicorn and D2_MPQ_DIR",
)
class StockTests(unittest.TestCase):
    """Run real stock functions and prove the checks detect broken instructions."""

    def setUp(self):
        """Use an isolated image so mutations cannot leak between checks."""
        import emu

        self.machine = emu.Emu()

    def test_stock_cases(self):
        """Cover new, current and legacy headers plus invalid rejection."""
        waypoint.check_new(self.machine, 0x1234, 0xA5)
        for header in (0, 0x101, 0x102, 0x103):
            waypoint.check_free(self.machine, 0x1234, header, 0xA5)

    def test_new_header_mutation_is_caught(self):
        """Changing the constructor's header immediate must fail."""
        self.machine.mu.mem_write(0x00660F5C, b"\x03")
        with self.assertRaisesRegex(AssertionError, "new block bytes"):
            waypoint.check_new(self.machine, 0, 0xA5)

    def test_new_zeroing_mutation_is_caught(self):
        """Omitting the last zero store must preserve dirt and fail."""
        self.machine.mu.mem_write(0x00660F56, b"\x90" * 3)
        with self.assertRaisesRegex(AssertionError, "new block bytes"):
            waypoint.check_new(self.machine, 0, 0xA5)

    def test_new_activation_mutation_is_caught(self):
        """Omitting activation of index zero must fail."""
        self.machine.mu.mem_write(0x00660F6C, b"\x90" * 4)
        with self.assertRaisesRegex(AssertionError, "new block bytes"):
            waypoint.check_new(self.machine, 0, 0xA5)

    def test_free_normalization_mutation_is_caught(self):
        """Changing the replacement header must fail at release."""
        self.machine.mu.mem_write(0x0066100C, b"\x03")
        with self.assertRaisesRegex(AssertionError, "normalized block"):
            waypoint.check_free(self.machine, 0, 0x101, 0xA5)

    def test_invalid_header_bypass_is_caught(self):
        """Forcing the current-header branch must not release an invalid block."""
        self.machine.mu.mem_write(0x00660FDD, b"\xeb")
        with self.assertRaises(AssertionError):
            waypoint.check_free(self.machine, 0, 0x103, 0xA5)

    def test_budget_exhaustion_is_caught(self):
        """A bounded call that never reaches its caller is a failure."""
        with self.assertRaisesRegex(AssertionError, "returned to caller"):
            waypoint.call(self.machine, 0x00660F30, 0, limit=1)


if __name__ == "__main__":
    unittest.main()
