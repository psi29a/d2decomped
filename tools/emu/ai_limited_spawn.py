# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify branches of FUN_005e0160's limited spawn think in 1.14d.

    uv run python ai_limited_spawn.py --exe /path/to/game.exe

Room, class, location, spawn and action helpers are scripted; no MPQs are read.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    game_address = machine.alloc(0x200)
    unit_address = machine.alloc(0x100)
    think_address = machine.alloc(0x40)
    control_address = machine.alloc(0x40)
    row_address = machine.alloc(0x100)
    tables_address = machine.alloc(0xC00)
    game = machine.view("game", game_address)
    think = machine.view("ai_think", think_address)
    control = machine.view("ai_control", control_address)
    row = machine.view("monstats_spawn", row_address)
    tables = machine.view("data_tables", tables_address)
    game.difficulty, game.frame = 0, 100
    think.control, think.monstats = control_address, row_address
    row.count, row.range, row.delay = 3, 10, 7
    machine.w32(0x744304, tables_address)
    calls = []
    scripted = {"location": True, "spawn": True}

    def room(current):
        calls.append(("room", current.arg(0)))
        return 0x1234

    def lookup(current):
        calls.append(("class", current.arg(0), current.arg(1)))
        return 0x77

    def location(current):
        calls.append(("location", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      current.arg(0), current.arg(1), current.arg(4)))
        current.w32(current.arg(2), 42)
        current.w32(current.arg(3), 53)
        return int(scripted["location"])

    def spawn(current):
        calls.append(("spawn", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      tuple(current.arg(i) for i in range(6))))
        return int(scripted["spawn"])

    def idle(current):
        calls.append(("idle", current.arg(0)))
        return 0

    def stop(current):
        calls.append(("stop", tuple(current.arg(i) for i in range(3))))
        return 0

    machine.hook(0x620BB0, room, 1)
    machine.hook(0x63EC70, lookup, 2)
    machine.hook(0x54DC40, location, 5)
    machine.hook(0x5B2F20, spawn, 6)
    machine.hook(0x5DE080, idle, 1)
    machine.hook(0x5DDFC0, stop, 3)
    cases = (("initialize", 0, 99, 11, 100, True, True, 3, 0),
             ("out-of-range", 4, 0, 11, 100, True, True, 0, 4),
             ("empty", 4, 0, 10, 100, True, True, 0, 4),
             ("not-due", 101, 2, 10, 100, True, True, 2, 101),
             ("location-fails", 100, 2, 10, 100, False, True, 2, 100),
             ("spawn-fails", 100, 2, 10, 100, True, False, 2, 100),
             ("spawn-success", 100, 2, 10, 100, True, True, 1, 107),
             ("missing-row", 99, 1, 10, 96, True, True, 0, 107))
    for label, timer, remaining, distance, table_count, can_place, can_spawn, expected_count, expected_timer in cases:
        control.scratch_one, control.scratch_two = timer, remaining
        think.distance, tables.spawn_class_count = distance, table_count
        scripted.update(location=can_place, spawn=can_spawn)
        calls.clear()
        machine.call(0x5E0160, think_address, ecx=game_address, edx=unit_address, limit=10000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError(f"{label}: did not return")
        if (control.scratch_two, control.scratch_one) != (expected_count, expected_timer):
            raise AssertionError(f"{label}: count/timer differs")
        expected = []
        if label in ("location-fails", "spawn-fails", "spawn-success", "missing-row"):
            expected = [("room", unit_address), ("class", 0x1234, 0x60 if table_count > 96 else 0xFFFFFFFF),
                        ("location", game_address, 0x1234, 0, 0x77, 0)]
            if can_place:
                expected.append(("spawn", game_address, 0x1234, (42, 53, 0x77, 1, 2, 0x42)))
        expected.append(("stop", (0, 0, 0)) if label == "empty" else ("idle", 15))
        if calls != expected:
            raise AssertionError(f"{label}: {calls!r} != {expected!r}")
    print(f"ok: {len(cases)} limited spawn decision cases")


if __name__ == "__main__":
    main()
