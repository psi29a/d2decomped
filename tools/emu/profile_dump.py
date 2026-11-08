# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify game.exe 1.14d's profile dump is dead (Logging\\ProfCore.cpp).

    uv run python profile_dump.py

FUN_006116b0 (D2ProfileDump, called on leaving the client mode at 0x44b980)
logs "PROFILING DUMP FREQUENCY" with QueryPerformanceFrequency, then one
line per row of the 23 0x78-byte rows at 0x743800 (name, the 64-bit time
at +0x68 of which the low dword is logged, count at +0x70) through
FUN_006248e0. Checked here:
- the stock dump logs every row as TIME 0 COUNT 0;
- a control: FUN_00611660 (ECX the row, a start on the stack) adds the
  QueryPerformanceCounter ticks since the start and 1 to the count, the next
  dump logs them, and 0x611730 dumps them, then zeroes the row;
- every instruction operand and aligned data dword that points into the rows
  sits in those three functions (0x611660..0x611752), so nothing else writes
  them;
- neither FUN_00611660 nor 0x611730 has a call, jump (rel32, or rel8 within
  reach) or pointer to it, so every row stays 0.
FUN_006248e0 is hooked (it writes only with a log file open). No MPQs are read.
"""
import argparse
import hashlib
import struct
from pathlib import Path

import capstone
import capstone.x86
import pefile
from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
DUMP = 0x6116B0
ACCUMULATE = 0x611660
DUMP_AND_RESET = 0x611730
LOG_LINE = 0x6248E0
PROFCORE_END = 0x611753
ROWS = 0x743800
ROW_SIZE = 0x78
ROW_COUNT = 23
FREQUENCY = 0x1234567
COUNTER = 0x9000
START = 0x1000
CONTROL_ROW = 5
LIMIT = 100000


def require(condition, message):
    """Stop when game.exe's code disagrees with the claim."""
    if not condition:
        raise RuntimeError(message)


def sections(exe):
    """(virtual address, bytes) of each section of the image."""
    image = pefile.PE(str(exe), fast_load=True)
    base = image.OPTIONAL_HEADER.ImageBase
    return [(base + section.VirtualAddress, section.get_data()) for section in image.sections]


def references(exe, target):
    """Every rel32 call / jmp / jcc and rel8 jmp / jcc to target, and every dword equal to it."""
    found = []
    for start, data in sections(exe):
        for offset in range(len(data) - 3):
            if struct.unpack_from("<I", data, offset)[0] == target:
                found.append(("pointer", start + offset))
            short = start + offset + 1 + struct.unpack_from("<b", data, offset)[0]
            if offset and (data[offset - 1] == 0xEB or 0x70 <= data[offset - 1] <= 0x7F) and short == target:
                found.append(("short", start + offset - 1))
            if offset and data[offset - 1] in (0xE8, 0xE9):
                if start + offset + 4 + struct.unpack_from("<i", data, offset)[0] == target:
                    found.append(("call" if data[offset - 1] == 0xE8 else "jmp", start + offset - 1))
            if offset > 1 and data[offset - 2] == 0x0F and 0x80 <= data[offset - 1] <= 0x8F:
                if start + offset + 4 + struct.unpack_from("<i", data, offset)[0] == target:
                    found.append(("jcc", start + offset - 2))
    return found


def row_pointers(exe):
    """Addresses of every instruction (immediate or displacement operand) and aligned data dword
    that points into the 23 rows. Code is decoded so opcode bytes that happen to read as a row
    address (e.g. c7 41 74 00 at 0x5933b4) don't count."""
    end = ROWS + ROW_SIZE * ROW_COUNT
    image = pefile.PE(str(exe), fast_load=True)
    base = image.OPTIONAL_HEADER.ImageBase
    disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    disassembler.detail = True
    found = []
    for section in image.sections:
        start, data = base + section.VirtualAddress, section.get_data()
        if not section.Characteristics & pefile.SECTION_CHARACTERISTICS["IMAGE_SCN_MEM_EXECUTE"]:
            found += [start + offset for offset in range(0, len(data) - 3, 4)
                      if ROWS <= struct.unpack_from("<I", data, offset)[0] < end]
            continue
        offset = 0
        while offset < len(data):
            decoded = next(disassembler.disasm(data[offset:offset + 16], start + offset), None)
            if decoded is None:  # undecodable byte (padding or data in .text): step over it
                offset += 1
                continue
            for operand in decoded.operands:
                value = operand.imm if operand.type == capstone.x86.X86_OP_IMM else \
                    operand.mem.disp if operand.type == capstone.x86.X86_OP_MEM else None
                if value is not None and ROWS <= value & 0xFFFFFFFF < end:
                    found.append(decoded.address)
            offset += decoded.size
    return found


def main():
    """Run the dump on the stock table, then scan for the row writers' callers."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")

    def frequency(machine):
        """QueryPerformanceFrequency(&out): a known value."""
        machine.w32(machine.arg(0), FREQUENCY)
        machine.w32(machine.arg(0) + 4, 0)
        return 1, 1

    def counter(machine):
        """QueryPerformanceCounter(&out): a known value, for the control."""
        machine.w32(machine.arg(0), COUNTER)
        machine.w32(machine.arg(0) + 4, 0)
        return 1, 1

    emu.Emu.w_QueryPerformanceFrequency = frequency
    emu.Emu.w_QueryPerformanceCounter = counter
    machine = emu.Emu(str(exe))
    lines = []

    def log_line(current):
        """cdecl FUN_006248e0(game, frame, client, type, file, line, format, ...): recorded."""
        fixed = [current.arg(i) for i in range(7)]
        lines.append((fixed[:4], current.cstr(fixed[4]), fixed[5], current.cstr(fixed[6]),
                      [current.arg(7 + i) for i in range(3)]))
        return 0

    def run(address, *args, ecx=0):
        """Call address and check it returned within the cap."""
        machine.call(address, *args, ecx=ecx, limit=LIMIT)
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{address:08x} stopped at the {LIMIT} instruction cap")

    machine.hook(LOG_LINE, log_line, 0)
    run(DUMP)

    require(len(lines) == 1 + ROW_COUNT, f"{len(lines)} log lines")
    head = lines[0]
    require(head[:4] == ([1, 2, 1, 0x80000], ".\\Logging\\ProfCore.cpp", 0x4E,
                         "PROFILING DUMP FREQUENCY %d:\n") and head[4][:2] == [FREQUENCY, 0],
            f"first line {head!r}")
    names = []
    for index, (prefix, file_name, line, fmt, args) in enumerate(lines[1:]):
        row = ROWS + ROW_SIZE * index
        require((prefix, file_name, line, fmt) == ([1, 2, 1, 0x80000], ".\\Logging\\ProfCore.cpp", 0x54,
                                                   "FUNCTION: %30s, TIME %12d, COUNT %5d"),
                f"row {index}: {lines[index + 1]!r}")
        require(args == [row, 0, 0], f"row {index}: arguments {args!r}")
        names.append(machine.cstr(row))
    require(names[0] == "Sample" and names[-1] == "Com Path Step", f"row names {names[0]!r}..{names[-1]!r}")

    # The control: a row filled by FUN_00611660 is logged, then reset by 0x611730.
    row = ROWS + ROW_SIZE * CONTROL_ROW
    run(ACCUMULATE, START, 0, ecx=CONTROL_ROW)
    require((machine.r32(row + 0x68), machine.r32(row + 0x6C), machine.r32(row + 0x70)) == (COUNTER - START, 0, 1),
            "FUN_00611660 didn't add the ticks and the count")
    for address in (DUMP, DUMP_AND_RESET):
        lines.clear()
        run(address)
        require(len(lines) == 1 + ROW_COUNT and lines[1 + CONTROL_ROW][4] == [row, COUNTER - START, 1],
                f"{address:08x}: the filled row logged as {lines[1 + CONTROL_ROW][4] if len(lines) > 1 + CONTROL_ROW else None}")
    require(all(machine.r32(row + offset) == 0 for offset in (0x68, 0x6C, 0x70)), "0x611730 didn't zero the row")

    pointers = row_pointers(exe)
    require(pointers and all(ACCUMULATE <= address < PROFCORE_END for address in pointers),
            f"row pointers outside ProfCore: {[hex(address) for address in pointers if not ACCUMULATE <= address < PROFCORE_END]}")

    for target in (ACCUMULATE, DUMP_AND_RESET):
        found = references(exe, target)
        require(not found, f"{target:08x} is referenced: {found}")
    dump_calls = references(exe, DUMP)
    require(sorted(kind_address for kind_address in dump_calls) == [("call", 0x44B980), ("call", 0x611730)],
            f"FUN_006116b0's callers: {dump_calls}")
    print(f"{len(names)} rows ({names[0]} .. {names[-1]}), each TIME 0 COUNT 0; the control row logged and reset; "
          f"{len(pointers)} row pointers, all in ProfCore; FUN_00611660 and 0x611730 have no call, jump or pointer")
    print("ok")


if __name__ == "__main__":
    main()
