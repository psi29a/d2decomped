# SPDX-License-Identifier: GPL-3.0-or-later
"""Run stock TBL formatters and opener on boundary and synthetic-file cases.

    uv run python strtable_formatters.py [--exe /path/to/Game.exe]

The optional executable avoids needing MPQs; otherwise emu.Emu uses its
configured 1.14d install. Results exercise FUN_00525350, FUN_00525490,
FUN_005255c0 and FUN_00525920.
"""
import argparse
import hashlib
import struct
from pathlib import Path

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
SUFFIXES = (0x730510, 0x730514, 0x730518, 0x73051C)
CRC_TABLE = 0x7301D8


class FatalError(Exception):
    """The hooked game fatal path was reached for an invalid synthetic TBL."""


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


def tbl_crc(machine, data, start, end):
    """Compute the stock TBL CRC-16 over a byte range."""
    crc = 0xFFFF
    for byte in data[start:end]:
        crc = ((crc & 0xFF) << 8) ^ machine.r16(CRC_TABLE + 2 * (((crc >> 8) ^ byte) & 0xFF))
    return crc


def check_tbl_open(machine):
    """Check success and each early error in FUN_00525920's CRC/offset checks."""
    address = machine.alloc(41)
    machine.hook(0x517079, lambda _emu: address, 3)
    fatal_line = [None]

    def make_tbl(key_offset=38, value_offset=39, corrupt_crc=False):
        """Build one TBL with caller-selected offsets and a valid CRC by default."""
        raw = bytearray(41)
        struct.pack_into("<H", raw, 2, 1)       # node count
        struct.pack_into("<I", raw, 4, 1)       # hash table size
        struct.pack_into("<I", raw, 9, 9)       # CRC start
        struct.pack_into("<I", raw, 17, len(raw))
        record = 21 + 2
        struct.pack_into("<I", raw, record + 7, key_offset)
        struct.pack_into("<I", raw, record + 11, value_offset)
        raw[38:41] = b"k\0v"
        crc = tbl_crc(machine, raw, 9, len(raw)) ^ int(corrupt_crc)
        struct.pack_into("<H", raw, 0, crc)
        machine.mu.mem_write(address, bytes(raw))

    def fatal(_emu):
        """Stop instead of executing the game's process-fatal handler."""
        fatal_line[0] = _emu.arg(1)
        raise FatalError

    machine.hook(0x408090, lambda hook: hook.arg(0), 1)
    machine.hook(0x408A60, fatal, 0)
    make_tbl()
    assert machine.call(0x525920) == address

    for expected_line, options in ((0xCE, {"corrupt_crc": True}),
                                   (0xE0, {"key_offset": 41}),
                                   (0xE1, {"value_offset": 41})):
        make_tbl(**options)
        try:
            machine.call(0x525920)
        except FatalError:
            assert fatal_line[0] == expected_line, (fatal_line[0], expected_line)
        else:
            raise AssertionError(f"FUN_00525920 accepted invalid TBL error case {expected_line:#x}")


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

    check_tbl_open(machine)
    print("ok: stock TBL formatters and CRC-checked opener cases")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", help="local stock 1.14d Game.exe; MPQs are not needed")
    run(parser.parse_args().exe)
