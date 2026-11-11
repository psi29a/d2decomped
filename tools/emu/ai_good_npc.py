# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005e7e20's friendly-NPC timer and combat decisions.

    uv run python ai_good_npc.py --exe /path/to/game.exe

Player and enemy lookup, status, random roll and actions are scripted.
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
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    game_address, unit_address, data_address, player_address = (machine.alloc(0x100) for _ in range(4))
    think_address, control_address = machine.alloc(0x40), machine.alloc(0x40)
    unit, player = machine.view("unit", unit_address), machine.view("unit", player_address)
    data, control = machine.view("monster_data", data_address), machine.view("ai_control", control_address)
    machine.view("ai_think", think_address).control = control_address
    unit.type, unit.data, data.state_gate = 1, data_address, 0xCAFE
    player.type = 0
    scripted = {"gate": 0, "player": player_address, "flag": 1, "status": 0, "roll": 1,
                "enemy": 0, "distance": 0}
    calls = []

    def gate(current):
        calls.append(("gate", current.mu.reg_read(UC_X86_REG_ECX)))
        return scripted["gate"]

    def get_player(current):
        calls.append(("player", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
        current.w32(current.arg(0), scripted["flag"])
        return scripted["player"]

    def status(current):
        calls.append(("status", current.mu.reg_read(UC_X86_REG_ECX)))
        return scripted["status"]

    def roll(current):
        calls.append(("roll", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
        return scripted["roll"]

    def enemy(current):
        calls.append(("enemy", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
        current.w32(current.arg(0), scripted["distance"])
        current.w32(current.arg(1), 0)
        return scripted["enemy"]

    def action(name, count):
        def callback(current):
            calls.append((name, current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                          tuple(current.arg(i) for i in range(count))))
            return 0
        return callback

    machine.hook(0x572DC0, gate, 0)
    machine.hook(0x5DDF20, get_player, 1)
    machine.hook(0x535060, status, 0)
    machine.hook(0x45C390, roll, 0)
    machine.hook(0x5DDC30, enemy, 2)
    machine.hook(0x553380, action("greet", 1), 1)
    machine.hook(0x5DDF90, action("attack", 2), 2)
    machine.hook(0x5DE080, action("idle", 1), 1)
    cases = (("gate", 1, player_address, 1, 0, 1, 0, 0, 0),
             ("greet", 0, player_address, 1, 0, 1, 0, 0, 0),
             ("decrement", 0, player_address, 1, 0, 1, 0, 0, 2),
             ("negative", 0, player_address, 1, 0, 1, 0, 0, 0xFFFFFFFF),
             ("no-player", 0, 0, 1, 0, 1, 0x7777, 24, 0),
             ("status", 0, player_address, 1, 1, 1, 0x7777, 25, 0),
             ("zero-roll", 0, player_address, 1, 0, 0, 0, 0, 0),
             ("zero-flag", 0, player_address, 0, 0, 1, 0, 0, 0))
    for label, active_gate, player_ptr, flag, status_value, random_value, enemy_ptr, distance, timer in cases:
        scripted.update(gate=active_gate, player=player_ptr, flag=flag, status=status_value,
                        roll=random_value, enemy=enemy_ptr, distance=distance)
        control.scratch_two = timer
        calls.clear()
        machine.call(0x5E7E20, think_address, ecx=game_address, edx=unit_address, limit=10000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError(f"{label}: did not return")
        expected = [("gate", 0xCAFE)]
        if label != "gate":
            expected.append(("player", game_address, unit_address))
        if label not in ("gate", "no-player"):
            expected.append(("status", player_address))
        if label in ("greet", "decrement", "negative", "zero-roll", "zero-flag"):
            expected.append(("roll", unit_address + 0x20, 3))
        if label in ("no-player", "status", "zero-roll", "zero-flag"):
            expected.append(("enemy", game_address, unit_address))
        if label == "greet":
            expected.append(("greet", unit_address, 0x12, (player_address,)))
        if label == "no-player":
            expected.append(("attack", game_address, unit_address, (4, enemy_ptr)))
        else:
            duration = 10 if label == "gate" else 20 if label in ("greet", "decrement", "negative") else 50
            expected.append(("idle", game_address, unit_address, (duration,)))
        expected_timer = 60 if label == "greet" else 1 if label == "decrement" else 0 if label == "negative" else timer
        if calls != expected or control.scratch_two != expected_timer:
            raise AssertionError(f"{label}: calls/state differ: {calls!r} != {expected!r}")
    print(f"ok: {len(cases)} friendly NPC timer and attack cases")


if __name__ == "__main__":
    main()
