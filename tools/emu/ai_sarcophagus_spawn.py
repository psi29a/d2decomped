# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005e0260's spawn-skill decision in stock 1.14d.

    uv run python ai_sarcophagus_spawn.py --exe /path/to/game.exe

Coordinates, room, placement and actions are hooked; no MPQs are read.
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
    game_address, unit_address = machine.alloc(0x200), machine.alloc(0x100)
    think_address, control_address = machine.alloc(0x40), machine.alloc(0x40)
    row_address, tables_address = machine.alloc(0x200), machine.alloc(0xC00)
    game, unit = machine.view("game", game_address), machine.view("unit", unit_address)
    think = machine.view("ai_think", think_address)
    control = machine.view("ai_control", control_address)
    row = machine.view("monstats_spawn", row_address)
    tables = machine.view("data_tables", tables_address)
    game.difficulty, game.frame = 0, 100
    think.control, think.monstats, think.target = control_address, row_address, 0x2468
    row.count, row.range, row.delay, row.skill, row.skill_mode = 3, 10, 7, 55, 6
    machine.w32(0x744304, tables_address)
    calls = []
    scripted = {"place": False}

    def coordinates(name, value):
        def callback(current):
            calls.append((name, current.mu.reg_read(UC_X86_REG_ECX)))
            return value
        return callback

    def room(current):
        calls.append(("room", current.arg(0)))
        return 0x1234

    def placement(current):
        calls.append(("placement", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      tuple(current.arg(i) for i in range(3))))
        return int(scripted["place"])

    def action(name, count):
        def callback(current):
            calls.append((name, current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                          tuple(current.arg(i) for i in range(count))))
            return 0
        return callback

    machine.hook(0x45AE20, coordinates("y", 53), 0)
    machine.hook(0x45ADF0, coordinates("x", 42), 0)
    machine.hook(0x620BB0, room, 1)
    machine.hook(0x5FD350, placement, 3)
    machine.hook(0x5DDFC0, action("stop", 3), 3)
    machine.hook(0x5DE080, action("idle", 1), 1)
    machine.hook(0x5DEAD0, action("cast", 5), 5)
    cases = (("range", 11, 4, 99, 55, 0x14F, True),
             ("exhausted", 10, 4, 99, 55, 0x14F, True),
             ("missing-skill", 10, 3, 99, 0xFFFF, 0x14F, True),
             ("not-due", 10, 3, 100, 55, 0x14F, True),
             ("placement-fails", 10, 3, 99, 55, 0x14F, False),
             ("cast", 10, 3, 99, 55, 0x14F, True),
             ("missing-row", 10, 3, 99, 55, 0x14E, True))
    for label, distance, count, due, skill, table_count, can_place in cases:
        think.distance, control.scratch_two, control.scratch_one = distance, count, due
        row.skill, tables.spawn_class_count, unit.flags = skill, table_count, 0x11
        scripted["place"] = can_place
        calls.clear()
        machine.call(0x5E0260, think_address, ecx=game_address, edx=unit_address, limit=10000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError(f"{label}: did not return")
        expected = []
        if label in ("placement-fails", "cast", "missing-row"):
            expected = [("y", unit_address), ("x", unit_address), ("room", unit_address),
                        ("placement", 0x14E if table_count > 0x14E else 0xFFFFFFFF,
                         0x1234, (42, 53, 0))]
        if label == "exhausted":
            expected.append(("stop", game_address, unit_address, (0, 0, 0)))
        elif label in ("cast", "missing-row"):
            expected.append(("cast", game_address, unit_address, (6, 55, think.target, 0, 0)))
        else:
            expected.append(("idle", game_address, unit_address, (25,)))
        expected_count = count + int(label in ("cast", "missing-row"))
        expected_timer = 107 if label in ("cast", "missing-row") else due
        if (calls, control.scratch_two, control.scratch_one, unit.flags) != (
            expected, expected_count, expected_timer, 0x20011 if label == "exhausted" else 0x11
        ):
            raise AssertionError(f"{label}: calls/state differ: {calls!r}")
    print(f"ok: {len(cases)} sarcophagus spawn decision cases")


if __name__ == "__main__":
    main()
