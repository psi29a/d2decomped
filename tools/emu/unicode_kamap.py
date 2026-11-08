# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the font map loader and the character count it drives, in stock game.exe 1.14d.

    uv run python unicode_kamap.py

FUN_00527790 (pool ECX, name EDX) reads a font's `default.map`: "KAMAP\\n", a
version line ended by 0x1a within 40 bytes, four bytes, then a u16 entry count,
a u8 range count, that many (first, last) byte ranges, the entries as u16
pairs, and the entry count's u16 code table. It keeps only the low byte of
each pair's two u16s (`movzx byte` at 0x527953 / 0x52795a). FUN_005276e0 (byte
in DL) is 1, a lead byte, for a byte from 0x20 outside every range.
FUN_00527d50 (text ECX, limit EDX) is strlen without a code table; with one it
counts characters, a lead byte taking two bytes, up to the limit.

Runs the installed Latin map (the English one: ranges 0x20..0x7e, 0xa0..0xff),
synthetic maps for the two format failure exits (0x65, 0x79; not the
allocation failures) and the low-byte truncation, every byte
through FUN_005276e0, and every English TBL value through FUN_00527d50 against
the units FUN_00526320 decodes from it. The file read, free and Storm
allocator are hooked; the MPQ is read through mpq-cat (D2_BUILD).
"""
import argparse
import hashlib
import struct
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EDX, UC_X86_REG_EIP

import emu
import tbl

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
LOAD_MAP = 0x527790
IS_LEAD = 0x5276E0
COUNT = 0x527D50
DECODE = 0x526320
READ_FILE = 0x517079
FREE_FILE = 0x40B3C0
SMEM_ALLOC = 0x413020
SMEM_FREE = 0x412650
STRTOK = 0x68264F          # CRT; its per-thread data doesn't start under the emulator
ENTRY_COUNT = 0x882B20   # u16
RANGE_COUNT = 0x882B22   # u8
RANGES = 0x882B28        # u8 pairs
ENTRIES = 0x882B24       # u16 pairs, low bytes only
CODES = 0x882B2C         # u16 per entry
VERSION = 0x882AF8
LATIN_MAP = "data\\local\\font\\latin\\default.map"
INSTRUCTION_LIMIT = 200_000


def require(condition, message):
    """Stop the probe when a required stock-code observation is absent."""
    if not condition:
        raise RuntimeError(message)


def parse(raw):
    """The map as FUN_00527790 reads it: (version, ranges, entries, codes)."""
    marker = raw.index(b"\x1a", 6)
    start = marker + 5
    count, range_count = struct.unpack_from("<HB", raw, start)
    ranges = [tuple(raw[start + 3 + 2 * i:start + 5 + 2 * i]) for i in range(range_count)]
    entries_at = start + 3 + 2 * range_count
    entries = [struct.unpack_from("<HH", raw, entries_at + 4 * i) for i in range(count)]
    codes = list(struct.unpack_from(f"<{count}H", raw, entries_at + 4 * count))
    return raw[6:marker].split(b"\n")[0].decode(), ranges, entries, codes


def build(version, ranges, entries, codes):
    """A synthetic map in the same layout."""
    body = struct.pack("<HB", len(entries), len(ranges)) + b"".join(bytes(pair) for pair in ranges)
    body += b"".join(struct.pack("<HH", *pair) for pair in entries) + struct.pack(f"<{len(codes)}H", *codes)
    return b"KAMAP\n" + version + b"\x1a" + bytes(4) + body


class Loader:
    """The hooked file read, file free and Storm allocator, recording each call."""

    def __init__(self, machine):
        """Hook the four callees on `machine`."""
        self.machine = machine
        self.file = b""
        self.reads, self.frees, self.storm_frees = [], [], []
        machine.hook(READ_FILE, self.read_file, 3)
        machine.hook(FREE_FILE, self.free_file, 2)
        machine.hook(SMEM_ALLOC, lambda current: current.alloc(current.arg(0)), 4)
        machine.hook(SMEM_FREE, self.storm_free, 4)
        machine.hook(STRTOK, self.strtok, 0)

    def read_file(self, current):
        """FUN_00517079: the name read, then the file's bytes in a fresh buffer."""
        self.reads.append(current.cstr(current.mu.reg_read(UC_X86_REG_EDX)))
        return current.put(self.file + b"\0")

    def free_file(self, current):
        """FUN_0040b3c0 (buffer ECX, file EDX, line and flag on the stack): record the line."""
        self.frees.append(current.arg(0))
        return 0

    @staticmethod
    def strtok(current):
        """strtok(text, delimiters), first call: skip leading delimiters, cut at the next (cdecl)."""
        text, delimiters = current.arg(0), set(current.cstr(current.arg(1)).encode())
        while current.read(text, 1)[0] in delimiters - {0}:
            text += 1
        if not current.read(text, 1)[0]:
            return 0
        end = text
        while (byte := current.read(end, 1)[0]) and byte not in delimiters:
            end += 1
        if byte:
            current.mu.mem_write(end, b"\0")
        return text

    def storm_free(self, current):
        """SMemFree: record the pointer."""
        self.storm_frees.append(current.arg(0))
        return 1

    def load(self, data, name="font\\map"):
        """Run FUN_00527790 on `data`; returns the line of the file free it ended with."""
        self.file = data
        frees = len(self.frees)
        self.machine.call(LOAD_MAP, ecx=0, edx=self.machine.put(name.encode() + b"\0"), limit=INSTRUCTION_LIMIT)
        require(self.machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "FUN_00527790 did not return")
        require(len(self.frees) == frees + 1, "FUN_00527790 did not free the file exactly once")
        return self.frees[-1]


def loaded(machine):
    """The globals FUN_00527790 set: (version, ranges, entries, codes)."""
    count = machine.r16(ENTRY_COUNT)
    range_count = machine.read(RANGE_COUNT, 1)[0]
    ranges_at, entries_at, codes_at = machine.r32(RANGES), machine.r32(ENTRIES), machine.r32(CODES)
    ranges = [tuple(machine.read(ranges_at + 2 * i, 2)) for i in range(range_count)]
    entries = [(machine.r16(entries_at + 4 * i), machine.r16(entries_at + 4 * i + 2)) for i in range(count)]
    codes = [machine.r16(codes_at + 2 * i) for i in range(count)]
    return machine.cstr(VERSION), ranges, entries, codes


def is_lead(machine, byte):
    """FUN_005276e0(byte in DL)."""
    value = machine.call(IS_LEAD, edx=byte, limit=INSTRUCTION_LIMIT)
    require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "FUN_005276e0 did not return")
    return value


def count(machine, text, limit):
    """FUN_00527d50(text ECX, limit EDX)."""
    value = machine.call(COUNT, ecx=machine.put(text + b"\0"), edx=limit, limit=INSTRUCTION_LIMIT)
    require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "FUN_00527d50 did not return")
    return value


def model_count(text, limit, ranges):
    """FUN_00527d50 with a code table loaded, from the ranges."""
    lead = [byte >= 0x20 and not any(first <= byte <= last for first, last in ranges) for byte in range(256)]
    characters, index = 0, 0
    while index < limit and index < len(text):
        index += 2 if lead[text[index]] else 1
        characters += 1
    return characters


def tbl_values(raw):
    """Every used node's value bytes and its u16 length.

    FUN_005259c0 also counts the unused, all-zero nodes; their count is 0.
    """
    nodes_number = struct.unpack_from("<H", raw, 2)[0]
    hash_size = struct.unpack_from("<I", raw, 4)[0]
    nodes_offset = 21 + nodes_number * 2
    for index in range(hash_size):
        node = nodes_offset + index * 17
        if raw[node]:
            value_offset = struct.unpack_from("<I", raw, node + 11)[0]
            length = struct.unpack_from("<H", raw, node + 15)[0]
            yield raw[value_offset:raw.index(0, value_offset)], length


def main():
    """Run the map, lead-byte and count cases."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(exe))
    loader = Loader(machine)
    cases = 0

    # Without a code table the count is strlen, the limit unread.
    machine.w32(CODES, 0)
    require(count(machine, b"a\x85bc", 2) == 4, "no code table: count isn't strlen")
    cases += 1

    # A synthetic map: each entry keeps its u16s' low bytes; the code table is whole.
    synthetic = (b"T1\nrest", [(0x41, 0x5A)], [(0x1234, 0x00E9), (0x41, 0x42)], [0x0107, 0xBEEF])
    require(loader.load(build(*synthetic)) == 0xBE, "synthetic map not loaded")
    require(loaded(machine) == ("T1", [(0x41, 0x5A)], [(0x34, 0xE9), (0x41, 0x42)], [0x0107, 0xBEEF]),
            f"synthetic map read as {loaded(machine)!r}")
    old = (machine.r32(RANGES), machine.r32(ENTRIES), machine.r32(CODES))
    cases += 1

    # The format failure exits free the file and change nothing: tables, counts, version.
    state = (old, loaded(machine), len(loader.storm_frees))
    require(loader.load(b"KAMAQ\n...") == 0x65, "bad magic")
    require((old, loaded(machine), len(loader.storm_frees)) == state, "bad magic changed the tables")
    require(loader.load(b"KAMAP\n" + b"x" * 64) == 0x79, "no 0x1a within 40 bytes")
    require((old, loaded(machine), len(loader.storm_frees)) == state, "a missing 0x1a changed the tables")
    cases += 2

    # The installed Latin map; loading again frees the three previous tables.
    raw = machine.mpq_file(LATIN_MAP)
    require(raw, f"{LATIN_MAP} not found")
    storm_frees = len(loader.storm_frees)
    require(loader.load(raw, LATIN_MAP) == 0xBE, "Latin map not loaded")
    require(loader.reads[-1] == LATIN_MAP, f"read {loader.reads[-1]!r}")
    require(loader.storm_frees[storm_frees:] == list(old), f"freed {loader.storm_frees[storm_frees:]!r}")
    version, ranges, entries, codes = parse(raw)
    require(loaded(machine) == (version, ranges, [(a & 0xFF, b & 0xFF) for a, b in entries], codes),
            "Latin map tables differ from the file")
    require(ranges == [(0x20, 0x7E), (0xA0, 0xFF)], f"Latin ranges {ranges}")
    cases += 1

    # Lead bytes under the Latin map: 0x7f..0x9f only.
    leads = [byte for byte in range(256) if is_lead(machine, byte)]
    require(leads == list(range(0x7F, 0xA0)), f"lead bytes {[hex(byte) for byte in leads]}")
    cases += 1

    # The count: a lead byte takes the next byte with it; the limit stops it.
    for text, limit, expected in ((b"Hello", 100, 5), (b"Hello", 3, 3), (b"a\x85bc", 100, 3), (b"\x85", 100, 1)):
        require(count(machine, text, limit) == expected == model_count(text, limit, ranges),
                f"count({text!r}, {limit})")
    cases += 1

    # Every English TBL value: the count is FUN_005259c0's buffer size, never below the decoded units.
    values = 0
    for name in ("string", "patchstring", "expansionstring"):
        table = machine.mpq_file(f"data\\local\\lng\\eng\\{name}.tbl")
        require(table, f"{name}.tbl not found")
        for text, length in tbl_values(table):
            characters = count(machine, text, length)
            require(characters == model_count(text, length, ranges), f"count of {text[:20]!r}")
            require(characters >= len(tbl.decode(machine, text + b"\0")), f"count below decode for {text[:20]!r}")
            values += 1
    cases += 1
    print(f"ok: {cases} font map / count cases; Latin lead bytes 0x7f..0x9f; "
          f"{values} English TBL values counted at or above their decoded length")


if __name__ == "__main__":
    main()
