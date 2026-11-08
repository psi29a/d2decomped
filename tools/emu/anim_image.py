# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify game.exe 1.14d's animated image control (D2WinAnimImage.cpp, FUN_00500850).

    uv run python anim_image.py

FUN_00500850 (fastcall: ECX x, EDX y; then size x, size y, DC6, frame
delay, on-press, animation frames, draw mode, notify; ret 0x20) allocates
0x60 bytes (D2WinAnimImage.cpp line 0x1c), zeroes them and fills: +0 type 2,
+4 the DC6, +8 state bits 0 and 2 (0x6ce268, 0x6ce270), +0xc / +0x10 x, y,
+0x14 / +0x18 the size, +0x1c draw 0x5005b0, +0x24 push 0x500790, +0x34
on-press, +0x38 0x5003a0 only with animation frames, +0x40 the frames, +0x44
the delay, +0x48 GetTickCount, +0x54 the draw mode (0 becomes 5, opaque),
+0x58 notify, +0x5c 1; then adds it to the control list (FUN_004f9210).
Run on each kind-3 screen record (0x708d10, 0x30 apart: +4 x, +8 y, +0xc
/ +0x10 size, +0x14 delay, +0x18 draw mode, +0x1c the DC6's cell (it passes
the DC6 the cell holds, **(record+0x1c), 0 for none), +0x20 on-press,
+0x24 frames, +0x28 notify), as FUN_004f93c0 passes them. The allocator,
memset, GetTickCount and the list add are hooked. No MPQs are read.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CREATE = 0x500850
ALLOC = 0x40B380
MEMSET = 0x681EF0
ADD_CONTROL = 0x4F9210
FILE_NAME = 0x6DC8A0
RECORDS = 0x708D10
RECORD_SIZE = 0x30
TICK = 0x1234
LIMIT = 10000
MAX_RECORDS = 512
SENTINEL_DC6 = 0xD0C6D0C6
FIRE = (0x70AE70, 0x70AEA0)


def require(condition, message):
    """Stop when game.exe's code disagrees with the claim."""
    if not condition:
        raise RuntimeError(message)


def kind3_records(machine):
    """The kind-3 rows of the screen record table (it ends at kind -1)."""
    found = []
    for index in range(MAX_RECORDS):
        record = RECORDS + RECORD_SIZE * index
        kind = machine.r32(record)
        if kind == 0xFFFFFFFF:
            return found
        require(1 <= kind <= 13, f"{record:#x}: kind {kind:#x}")
        if kind == 3:
            found.append(record)
    raise RuntimeError(f"no end marker in {MAX_RECORDS} records")


def main():
    """Build a control from each kind-3 record and check every field."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    emu.Emu.w_GetTickCount = lambda machine: (TICK, 0)
    machine = emu.Emu(str(exe))
    require(machine.cstr(FILE_NAME).lower().endswith("d2winanimimage.cpp"), "0x6dc8a0 is not D2WinAnimImage.cpp")
    calls = {"alloc": [], "added": []}

    def alloc(current):
        """Game alloc (ECX size, EDX file, line, flag): poisoned so the memset shows."""
        size = current.mu.reg_read(UC_X86_REG_ECX)
        calls["alloc"].append((size, current.mu.reg_read(UC_X86_REG_EDX), current.arg(0)))
        block = current.alloc(size)
        current.mu.mem_write(block, b"\xab" * size)
        return block

    def memset(current):
        """cdecl memset(dest, value, size)."""
        current.mu.mem_write(current.arg(0), bytes([current.arg(1) & 0xFF]) * current.arg(2))
        return current.arg(0)

    machine.hook(ALLOC, alloc, 2)
    machine.hook(MEMSET, memset, 0)
    machine.hook(ADD_CONTROL, lambda current: calls["added"].append(current.arg(0)), 1)
    state = machine.r32(0x6CE268) | machine.r32(0x6CE270)
    require(state == 5, f"state masks {state:#x}")

    records = kind3_records(machine)
    require(len(records) == 15, f"{len(records)} kind-3 records")
    modes = {}
    # The DC6 cells are runtime-filled (0 in the image): a sentinel shows +4 is the argument.
    # The last run passes a DC6 for a record with no cell, as a control.
    control = next(record for record in records if not machine.r32(record + 0x1C))
    runs = [(record, False) for record in records] + [(control, True)]
    for record, forced in runs:
        field = [machine.r32(record + 4 * index) for index in range(11)]
        x, y, width, height, delay, mode, cell, on_press, frames, notify = field[1:]
        if record in FIRE:
            require(delay == 1 and mode == 3 and cell, f"{record:#x}: delay {delay}, mode {mode}, cell {cell:#x}")
        else:
            require(mode == 0, f"{record:#x}: record draw mode {mode}")
        if cell:
            machine.w32(cell, SENTINEL_DC6)
        # FUN_004f93c0 loads through +0x1c twice: ECX = **(record +0x1c) (the DC6 the cell holds), or 0
        dc6 = SENTINEL_DC6 if forced else machine.r32(cell) if cell else 0
        calls["alloc"].clear()
        calls["added"].clear()
        built = machine.call(CREATE, width, height, dc6, delay, on_press, frames, mode, notify,
                               ecx=x, edx=y, limit=LIMIT)
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{record:#x}: stopped at the cap")
        require(calls["alloc"] == [(0x60, FILE_NAME, 0x1C)], f"{record:#x}: alloc {calls['alloc']}")
        require(calls["added"] == [built], f"{record:#x}: list add {calls['added']}")
        got = [machine.r32(built + 4 * index) for index in range(0x18)]
        want = [2, dc6, state, x, y, width, height, 0x5005B0, 0, 0x500790, 0, 0, 0, on_press,
                0x5003A0 if frames else 0, 0, frames, delay, TICK, 0, 0, mode or 5, notify, 1]
        require(got == want, f"{record:#x}: fields {[hex(v) for v in got]}, expected {[hex(v) for v in want]}")
        if not forced:
            modes[record] = got[0x15]
    require(all(modes[record] == 3 for record in FIRE), "the campfire records aren't draw mode 3")
    require(all(mode == 5 for record, mode in modes.items() if record not in FIRE), f"draw modes {modes}")
    print(f"{len(records)} kind-3 records: the campfire (0x70ae70, 0x70aea0) draw mode 3, "
          "frame delay 1; the other 13 pass 0, stored as 5; the DC6 lands at +4")
    print("ok")


if __name__ == "__main__":
    main()
