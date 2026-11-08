# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the stock TBL number formatters on boundary values.

    uv run python strtable_formatters.py [--exe /path/to/Game.exe]

The optional executable avoids needing MPQs; otherwise emu.Emu uses its
configured 1.14d install. Results exercise FUN_00525350, FUN_00525490 and
FUN_005255c0.
"""
import argparse
import hashlib
import struct
from pathlib import Path

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
SUFFIXES = (0x730510, 0x730514, 0x730518, 0x73051C)


def text_units(machine, address):
    """Read a NUL-terminated UTF-16 string from emulated memory."""
    units = []
    for index in range(128):
        unit = machine.r16(address + index * 2)
        if unit == 0:
            return "".join(map(chr, units))
        units.append(unit)
    raise AssertionError("formatter output has no NUL within 128 units")


def formatter(machine, address, *arguments):
    """Run one formatter with a fresh zeroed destination and return its text."""
    destination = machine.alloc(256)
    machine.mu.mem_write(destination, b"\0" * 256)
    machine.call(address, destination, *arguments)
    return text_units(machine, destination)


def run(executable=None):
    """Check unsigned, signed and abbreviated formatters against exact outputs."""
    binary = Path(executable) if executable else Path(emu.data_path()) / "bin" / "game.exe"
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    assert digest == GAME_SHA256, ("unexpected Game.exe", digest)
    machine = emu.Emu(exe=executable)
    for address, suffix in zip(SUFFIXES, "KMBT"):
        machine.mu.mem_write(address, struct.pack("<H", ord(suffix)))

    for value, expected in (
        (0, "0"), (999, "999"), (1000, "1,000"),
        (1234567890, "1,234,567,890"), (0xFFFFFFFF, "4,294,967,295"),
    ):
        actual = formatter(machine, 0x525350, value, 32)
        assert actual == expected, ("FUN_00525350", value, actual, expected)

    for low, high, expected in (
        (0, 0, "0"), (1, 0, "1"), (0xFFFFFFFF, 0xFFFFFFFF, "-1"),
        (0xB669FD2E, 0xFFFFFFFF, "-1,234,567,890"),
        (0xFFFFFFFF, 0x7FFFFFFF, "9,223,372,036,854,775,807"),
        (0, 0x80000000, "--9,223,372,036,854,775,808"),
    ):
        actual = formatter(machine, 0x525490, low, high, 32)
        assert actual == expected, ("FUN_00525490", high, low, actual, expected)

    for low, high, expected in (
        (0, 0, "0"), (999, 0, "999"), (1000, 0, "1,000"),
        (1000000, 0, "1,000K"), (1234567890, 0, "1,234M"),
        (0xFFFFFFFF, 0xFFFFFFFF, "18,446,744T"),
    ):
        actual = formatter(machine, 0x5255C0, low, high, 32)
        assert actual == expected, ("FUN_005255c0", high, low, actual, expected)

    print("ok: stock TBL unsigned, signed, and K/M/B/T formatter cases")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", help="local stock 1.14d Game.exe; MPQs are not needed")
    run(parser.parse_args().exe)
