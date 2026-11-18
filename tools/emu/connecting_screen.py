# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify game.exe's FUN_0042f6e0 connecting-screen records and effects.

    python connecting_screen.py [--exe PATH] [--mutation-check]

All callees are hooked.  Both palette branches are checked for record ids,
call order and arguments, the PLEASE WAIT string id, and the three globals.
No MPQs are read.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MUTATIONS = ("wrong-record", "visible-cancel", "wrong-string", "missing-callback")
mutation = None


def run(exe):
    """Run both palette branches and compare every boundary effect."""
    machine = emu.Emu(str(exe))
    log = []
    handles = iter(())

    machine.hook(0x42F3F0, lambda current: (log.append(("clear",)), 0)[1], 0)

    def make(current):
        log.append(("make", current.mu.reg_read(UC_X86_REG_ECX)))
        return next(handles)

    machine.hook(0x42F3A0, make, 0)
    machine.hook(0x4FA470, lambda current: (log.append(("visible", current.arg(0), current.arg(1))), 0)[1], 2)
    machine.hook(0x4FA420, lambda current: (log.append(("enable", current.arg(0), current.arg(1))), 0)[1], 2)

    def string(current):
        log.append(("string", current.mu.reg_read(UC_X86_REG_ECX)))
        return 0x12345678

    def draw(current):
        log.append(("draw", current.mu.reg_read(UC_X86_REG_EAX),
                    current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
        return 0

    machine.hook(0x524A30, string, 0)
    machine.hook(0x4FD060, draw, 0)

    for palette, records in ((0, (0xDE, 0xDF, 0xE0)), (1, (0xF7, 0xFA, 0xF9))):
        nonlocal_handles = (0x1111, 0x2222, 0x3333)
        handles = iter(nonlocal_handles)
        log.clear()
        machine.w32(0x7795F0, palette)
        machine.w32(0x779954, 0)
        machine.w32(0x779974, 0xFFFFFFFF)
        machine.w32(0x7795BC, 0)
        machine.call(0x42F6E0, limit=1000)
        expected_records = records if mutation != "wrong-record" else (records[0], records[2], records[1])
        visible = 1 if mutation == "visible-cancel" else 0
        string_id = 0x147A if mutation == "wrong-string" else 0x147B
        expected = [
            ("clear",), ("make", expected_records[0]), ("visible", 0x1111, visible),
            ("make", expected_records[1]), ("make", expected_records[2]),
            ("enable", 0x1111, 1), ("enable", 0x2222, 1), ("enable", 0x3333, 1),
            ("string", string_id), ("draw", 0x12345678, 0x3333, 0x12345678),
        ]
        callback = 0 if mutation == "missing-callback" else 0x42F6C0
        globals_now = (machine.r32(0x779954), machine.r32(0x779974), machine.r32(0x7795BC))
        if log != expected or globals_now != (1, 0, callback):
            raise AssertionError(f"palette {palette}: calls {log}, globals {globals_now}")
    return "ok: FUN_0042f6e0 connecting screen (both palettes)"


def main():
    """Run the oracle and require each intentional defect to fail."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    exe = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    print(run(exe))
    if args.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run(exe)
            except AssertionError:
                continue
            raise AssertionError(f"mutation survived: {name}")
        mutation = None
        print(f"mutation check ok: {len(MUTATIONS)} defects rejected")


if __name__ == "__main__":
    main()
