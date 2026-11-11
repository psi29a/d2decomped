# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005e7ac0's NPC ranged decisions in stock 1.14d.

    uv run python ai_npc_ranged.py --exe /path/to/game.exe

Room, town, search, bounded roll and actions are scripted; no MPQs are read.
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
    game_address, unit_address, think_address, row_address = (machine.alloc(0x200) for _ in range(4))
    unit = machine.view("unit", unit_address)
    machine.view("ai_think", think_address).monstats = row_address
    row = machine.view("monstats_spawn", row_address)
    row.skill, row.skill_mode = 55, 6
    scripted = {"town": 0, "target": 0, "distance": 0, "rolls": []}
    calls = []

    def room(current):
        calls.append(("room", current.arg(0)))
        return 0x1234

    def town(current):
        calls.append(("town", current.arg(0)))
        return scripted["town"]

    def search(current):
        calls.append(("search", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
        current.w32(current.arg(0), scripted["distance"])
        current.w32(current.arg(1), 0)
        return scripted["target"]

    def roll(current):
        calls.append(("roll", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
        return scripted["rolls"].pop(0)

    def action(name, count):
        def callback(current):
            calls.append((name, current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                          tuple(current.arg(i) for i in range(count))))
            return 0
        return callback

    machine.hook(0x620BB0, room, 1)
    machine.hook(0x61AB00, town, 1)
    machine.hook(0x5DDC30, search, 2)
    machine.hook(0x45C390, roll, 0)
    for address, name, count in ((0x5DDF90, "attack", 2), (0x5DEAD0, "cast", 5),
                                 (0x5DF7D0, "move", 3), (0x5DE080, "idle", 1),
                                 (0x5DE200, "wander", 1)):
        machine.hook(address, action(name, count), count)
    cases = (("null", 0, 1, 0, 0, 0, 0, ()), ("mode", unit_address, 2, 0, 0, 0, 0, ()),
             ("attack", unit_address, 1, 0, 1, 0x7777, 19, (29,)),
             ("cast", unit_address, 1, 0, 0x10F, 0x7777, 19, (29,)),
             ("move", unit_address, 1, 0, 1, 0x7777, 19, (30, 29)),
             ("idle", unit_address, 1, 0, 1, 0x7777, 19, (30, 30)),
             ("far", unit_address, 1, 0, 1, 0x7777, 20, ()),
             ("town", unit_address, 1, 1, 1, 0, 0, ()))
    for label, pointer, mode, in_town, unit_class, target, distance, rolls in cases:
        unit.mode, unit["class"], unit.seed_low, unit.seed_high = mode, unit_class, 1, 666
        scripted.update(town=in_town, target=target, distance=distance, rolls=list(rolls))
        calls.clear()
        machine.call(0x5E7AC0, think_address, ecx=game_address, edx=pointer, limit=10000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError(f"{label}: did not return")
        expected = []
        if label not in ("null", "mode"):
            expected.extend((("room", unit_address), ("town", 0x1234)))
            if not in_town:
                expected.append(("search", game_address, unit_address))
                for _ in rolls:
                    expected.append(("roll", unit_address + 0x20, 100))
        if label == "attack":
            expected.append(("attack", game_address, unit_address, (4, target)))
        elif label == "cast":
            expected.append(("cast", game_address, unit_address, (6, 55, target, 0, 0)))
        elif label == "move":
            expected.append(("move", game_address, unit_address, (target, 4, 0)))
        elif label in ("far", "town"):
            stepped = 0x6AC690C5 + 666
            if (unit.seed_low, unit.seed_high) != (stepped & 0xFFFFFFFF, stepped >> 32):
                raise AssertionError(f"{label}: seed differs")
            action_name, delay = ("wander", 5) if (stepped & 0xFFFFFFFF) % 100 < 20 else ("idle", 10)
            expected.append((action_name, game_address, unit_address, (delay,)))
        else:
            expected.append(("idle", game_address, pointer, (5 if label in ("null", "mode") else 10,)))
        if calls != expected or scripted["rolls"]:
            raise AssertionError(f"{label}: {calls!r} != {expected!r}")
    print(f"ok: {len(cases)} NPC ranged decision cases")


if __name__ == "__main__":
    main()
