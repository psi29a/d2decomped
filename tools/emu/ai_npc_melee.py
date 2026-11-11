# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005e7c10's NPC melee decisions in stock 1.14d.

    uv run python ai_npc_melee.py --exe /path/to/game.exe

Room, town, target, random roll and action helpers are scripted; no MPQs are read.
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
    game_address, unit_address, think_address, control_address = (machine.alloc(0x100) for _ in range(4))
    unit = machine.view("unit", unit_address)
    machine.view("ai_think", think_address).control = control_address
    scripted = {"town": 0, "target": 0, "melee": 0, "roll": 0}
    calls = []

    def room(current):
        calls.append(("room", current.arg(0)))
        return 0x1234

    def town(current):
        calls.append(("town", current.arg(0)))
        return scripted["town"]

    def target(current):
        calls.append(("target", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX), current.arg(0)))
        current.w32(current.arg(1), 0)
        current.w32(current.arg(2), scripted["melee"])
        return scripted["target"]

    def roll(current):
        calls.append(("roll", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
        return scripted["roll"]

    def action(name, count):
        def callback(current):
            calls.append((name, current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                          tuple(current.arg(i) for i in range(count))))
            return 0
        return callback

    machine.hook(0x620BB0, room, 1)
    machine.hook(0x61AB00, town, 1)
    machine.hook(0x5DD7F0, target, 3)
    machine.hook(0x45C390, roll, 0)
    machine.hook(0x5DDF90, action("attack", 2), 2)
    machine.hook(0x5DEC80, action("move", 2), 2)
    machine.hook(0x5DE080, action("idle", 1), 1)
    machine.hook(0x5DE200, action("wander", 1), 1)
    cases = (("null", 0, 1, 0, 0, 0, 0, 1), ("mode", unit_address, 2, 0, 0, 0, 0, 1),
             ("move", unit_address, 1, 0, 0x7777, 0, 29, 1),
             ("far-idle", unit_address, 1, 0, 0x7777, 0, 30, 1),
             ("attack", unit_address, 1, 0, 0x7777, 1, 79, 1),
             ("near-idle", unit_address, 1, 0, 0x7777, 1, 80, 1),
             ("town-wander", unit_address, 1, 1, 0, 0, 0, 1),
             ("no-target-idle", unit_address, 1, 0, 0, 0, 0, 2))
    for label, pointer, mode, in_town, target_pointer, melee, value, seed in cases:
        unit.mode, unit.seed_low, unit.seed_high = mode, seed, 666
        scripted.update(town=in_town, target=target_pointer, melee=melee, roll=value)
        calls.clear()
        machine.call(0x5E7C10, think_address, ecx=game_address, edx=pointer, limit=10000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError(f"{label}: think did not return")
        expected = []
        if label not in ("null", "mode"):
            expected.extend((("room", unit_address), ("town", 0x1234)))
            if not in_town:
                expected.append(("target", game_address, unit_address, control_address))
                if target_pointer:
                    expected.append(("roll", unit_address + 0x20, 100))
        if label == "move":
            expected.append(("move", game_address, unit_address, (target_pointer, 7)))
        elif label == "attack":
            expected.append(("attack", game_address, unit_address, (4, target_pointer)))
        else:
            idle_value = 5 if label in ("null", "mode") else 10
            if label in ("town-wander", "no-target-idle"):
                stepped = seed * 0x6AC690C5 + 666
                if (unit.seed_low, unit.seed_high) != (stepped & 0xFFFFFFFF, stepped >> 32):
                    raise AssertionError(f"{label}: seed differs")
                if (stepped & 0xFFFFFFFF) % 100 < 20:
                    expected.append(("wander", game_address, unit_address, (5,)))
                else:
                    expected.append(("idle", game_address, unit_address, (10,)))
            else:
                expected.append(("idle", game_address, pointer, (idle_value,)))
        if calls != expected:
            raise AssertionError(f"{label}: {calls!r} != {expected!r}")
    print(f"ok: {len(cases)} NPC melee decision cases")


if __name__ == "__main__":
    main()
