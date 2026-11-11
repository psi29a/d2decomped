# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005fb410's mode and spawn decisions in stock 1.14d.

    uv run python ai_spawner_lay.py --exe /path/to/game.exe

Placement, room lookup, spawn, state and action helpers are scripted.
No MPQs are read.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    game_address, unit_address, spawned_address, think_address, row_address = (machine.alloc(0x200) for _ in range(5))
    control_address = machine.alloc(0x40)
    game, unit = machine.view("game", game_address), machine.view("unit", unit_address)
    spawned = machine.view("unit", spawned_address)
    control, think = machine.view("ai_control", control_address), machine.view("ai_think", think_address)
    row = machine.view("monstats_spawn", row_address)
    game.difficulty, game.frame = 0, 100
    think.control, think.monstats = control_address, row_address
    row.count, row.range = 3, 7
    calls = []
    scripted = {"room": True, "spawn": True}

    def placement(current):
        calls.append(("placement", current.arg(0), current.arg(5), current.arg(6)))
        for index, value in ((1, 77), (2, 42), (3, 53), (4, 88)):
            current.w32(current.arg(index), value)
        return 0

    def room(current):
        calls.append(("room", current.arg(0)))
        return 0x1234

    def nearest(current):
        calls.append(("nearest", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX), current.arg(0)))
        return 0x2468 if scripted["room"] else 0

    def spawn(current):
        calls.append(("spawn", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      tuple(current.arg(i) for i in range(6))))
        return spawned_address if scripted["spawn"] else 0

    def action(name, count, registers=True):
        def callback(current):
            prefix = (current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)) if registers else ()
            calls.append((name, *prefix, tuple(current.arg(i) for i in range(count))))
            return 0
        return callback

    machine.hook(0x63EFA0, placement, 7)
    machine.hook(0x620BB0, room, 1)
    machine.hook(0x463740, nearest, 1)
    machine.hook(0x5B2F20, spawn, 6)
    machine.hook(0x639DB0, action("state", 3, False), 3)
    machine.hook(0x621E40, action("stat", 3, False), 3)
    machine.hook(0x5DDFC0, action("mode", 3), 3)
    machine.hook(0x5DE080, action("idle", 1), 1)
    machine.hook(0x5DE0F0, action("wait", 1), 1)
    cases = (("initialize-far", 1, 6, 0, 99, 0, True, True),
             ("mode-one", 1, 5, 50, 2, 0, True, True),
             ("mode-ten", 10, 0, 50, 2, 0, True, True),
             ("not-due", 11, 0, 100, 2, 0, True, True),
             ("no-room", 11, 0, 99, 2, 0, False, True),
             ("spawn-fails", 11, 0, 99, 2, 0, True, False),
             ("spawn", 11, 0, 99, 2, 0, True, True),
             ("special", 11, 0, 99, 2, 0x2C7, True, True),
             ("empty", 11, 0, 99, 0, 0, True, True))
    for label, mode, distance, timer, count, unit_class, room_ok, spawn_ok in cases:
        unit.mode, unit["class"], spawned.flags = mode, unit_class, 0x11
        think.distance, control.scratch_one, control.scratch_two = distance, timer, count
        scripted.update(room=room_ok, spawn=spawn_ok)
        calls.clear()
        machine.call(0x5FB410, think_address, ecx=game_address, edx=unit_address, limit=10000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError(f"{label}: did not return")
        expected = []
        if label in ("no-room", "spawn-fails", "spawn", "special"):
            expected.extend((("placement", unit_address, 0, 0), ("room", unit_address), ("nearest", 0x1234, 42, 53)))
            if room_ok:
                expected.append(("spawn", game_address, 0x2468, (42, 53, 77, 88, 2, 0x42)))
            if label == "special":
                expected.extend((("state", (spawned_address, 0xB8, 1)), ("stat", (spawned_address, 0xCA, 0))))
        if label == "initialize-far":
            expected.append(("idle", game_address, unit_address, (5,)))
        elif label in ("mode-one", "mode-ten"):
            expected.extend((("mode", game_address, unit_address, (10 if mode == 1 else 11, 0, 0)),
                             ("wait", game_address, unit_address, (20,))))
        elif label == "empty":
            expected.append(("mode", game_address, unit_address, (0, 0, 0)))
        else:
            expected.append(("wait", game_address, unit_address, (7,)))
        expected_timer = 107 if label in ("initialize-far", "no-room", "spawn-fails", "spawn", "special") else timer
        expected_count = 3 if label == "initialize-far" else count - int(label in ("spawn", "special"))
        expected_flags = 0x4020011 if label in ("spawn", "special") else 0x11
        if calls != expected or (control.scratch_one, control.scratch_two, spawned.flags) != (expected_timer, expected_count, expected_flags):
            raise AssertionError(f"{label}: calls/state differ: {calls!r} != {expected!r}")
    print(f"ok: {len(cases)} spawner mode and spawn cases")


if __name__ == "__main__":
    main()
