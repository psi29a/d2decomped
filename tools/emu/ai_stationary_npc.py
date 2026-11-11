# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005e8340 stationary NPC follow and attack decisions.

    uv run python ai_stationary_npc.py --exe /path/to/game.exe

Owner, room, town, targeting and actions are scripted; no MPQs are read.
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
    game_address, unit_address, owner_address, data_address = (machine.alloc(0x200) for _ in range(4))
    think_address, control_address = machine.alloc(0x40), machine.alloc(0x40)
    game, unit = machine.view("game", game_address), machine.view("unit", unit_address)
    data, control = machine.view("monster_data", data_address), machine.view("ai_control", control_address)
    machine.view("ai_think", think_address).control = control_address
    game.frame, unit.type, unit.data, data.ai_control = 123, 1, data_address, control_address
    scripted = {"owner": 0, "town": 0, "hit": 0, "target": 0, "melee": 0}
    calls = []

    def owner(current):
        calls.append(("owner", current.arg(0), current.arg(1)))
        return scripted["owner"]

    def room(current):
        calls.append(("room", current.arg(0)))
        return 0x1234

    def town(current):
        calls.append(("town", current.arg(0)))
        return scripted["town"]

    def hit(current):
        calls.append(("hit", tuple(current.arg(i) for i in range(3))))
        return scripted["hit"]

    def target(current):
        calls.append(("target", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX), current.arg(0)))
        current.w32(current.arg(1), 0)
        current.w32(current.arg(2), scripted["melee"])
        return scripted["target"]

    def action(name, count):
        def callback(current):
            calls.append((name, current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                          tuple(current.arg(i) for i in range(count))))
            return 0
        return callback

    def orient(current):
        calls.append(("orient", current.arg(0), current.arg(1)))
        return 0

    machine.hook(0x553540, owner, 2)
    machine.hook(0x620BB0, room, 1)
    machine.hook(0x61AB00, town, 1)
    machine.hook(0x622C40, hit, 3)
    machine.hook(0x5DE890, target, 3)
    machine.hook(0x620C10, orient, 2)
    for address, name, count in ((0x5DED40, "follow", 2), (0x5DDF90, "attack", 2),
                                 (0x5B0E00, "switch", 2), (0x540E60, "event", 2),
                                 (0x5417D0, "sound", 4)):
        machine.hook(address, action(name, count), count)
    cases = (("no-owner", 0, 0, 0, 0, 0, 0), ("town", owner_address, 1, 0, 0, 0, 0),
             ("initial", owner_address, 0, 0, 0, 0, 0), ("owner-hit", owner_address, 0, 1, 1, 0, 0),
             ("other-hit", owner_address, 0, 1, 0, 0x7777, 1),
             ("follow", owner_address, 0, 1, 0, 0x7777, 0))
    for label, owner_ptr, in_town, scratch, in_reach, target_ptr, melee in cases:
        scripted.update(owner=owner_ptr, town=in_town, hit=in_reach, target=target_ptr, melee=melee)
        control.scratch_one = scratch
        calls.clear()
        machine.call(0x5E8340, think_address, ecx=game_address, edx=unit_address, limit=10000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError(f"{label}: did not return")
        expected = [("owner", game_address, unit_address)]
        if owner_ptr:
            expected.extend((("room", owner_ptr), ("town", 0x1234)))
        if label in ("no-owner", "town"):
            expected.extend((("switch", game_address, unit_address, (control_address, 0)),
                             ("event", game_address, unit_address, (2, 0)),
                             ("sound", game_address, unit_address, (2, 124, 0, 0))))
        elif label == "initial":
            expected.append(("follow", game_address, unit_address, (owner_ptr, 7)))
        else:
            expected.append(("hit", (unit_address, owner_ptr, 0)))
            if label == "owner-hit":
                expected.append(("attack", game_address, unit_address, (4, owner_ptr)))
            else:
                expected.append(("target", game_address, unit_address, control_address))
                if label == "other-hit":
                    expected.append(("attack", game_address, unit_address, (4, target_ptr)))
                else:
                    expected.extend((("orient", unit_address, owner_ptr),
                                     ("follow", game_address, unit_address, (owner_ptr, 7))))
        expected_scratch = 1 if label in ("initial", "follow") else 0 if label == "other-hit" else scratch
        if calls != expected or control.scratch_one != expected_scratch:
            raise AssertionError(f"{label}: {calls!r} != {expected!r}")
    print(f"ok: {len(cases)} stationary NPC decisions")


if __name__ == "__main__":
    main()
