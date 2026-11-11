# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005ea7a0's pet-follow timing and coordinate draws.

    uv run python ai_pet_follow.py --exe /path/to/game.exe

Owner lookup, signal and actions are scripted; no MPQs are read.
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
    game_address, unit_address, owner_address, path_address, row_address = (machine.alloc(0x200) for _ in range(5))
    think_address, control_address = machine.alloc(0x40), machine.alloc(0x40)
    game, unit, owner = (machine.view("game", game_address), machine.view("unit", unit_address), machine.view("unit", owner_address))
    path, row = machine.view("static_path", path_address), machine.view("monstats_spawn", row_address)
    control, think = machine.view("ai_control", control_address), machine.view("ai_think", think_address)
    game.frame, game.difficulty = 100, 0
    owner.type, owner.path, path.x, path.y = 2, path_address, 100, 90
    think.control, think.monstats = control_address, row_address
    row.range = 7
    calls = []
    scripted = {"signal": 0}

    def owner_lookup(current):
        calls.append(("owner", current.mu.reg_read(UC_X86_REG_ECX)))
        return owner_address

    def signal(current):
        calls.append(("signal", current.mu.reg_read(UC_X86_REG_ECX)))
        return scripted["signal"]

    def action(name, count):
        def callback(current):
            calls.append((name, current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                          tuple(current.arg(i) for i in range(count))))
            return 0
        return callback

    machine.hook(0x58F0D0, owner_lookup, 0)
    machine.hook(0x5DD2B0, signal, 0)
    machine.hook(0x5DE080, action("idle", 1), 1)
    machine.hook(0x5DEAD0, action("cast", 5), 5)
    cases = (("wait", 101, 1, 0, 4), ("equal", 100, 1, 0, 4),
             ("signaled", 100, 1, 1, 4), ("due", 99, 1, 0, 3),
             ("owner-mode-three", 101, 3, 0, 5), ("zero-radius", 99, 1, 0, 0),
             ("negative-radius", 99, 1, 0, -2))
    for label, timer, mode, signal_value, radius in cases:
        owner.mode, control.scratch_one = mode, timer
        row.count = radius & 0xFFFF
        unit.seed_low, unit.seed_high = 0x12345678, 666
        scripted["signal"] = signal_value
        calls.clear()
        machine.call(0x5EA7A0, think_address, ecx=game_address, edx=unit_address, limit=10000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError(f"{label}: did not return")
        expected = [("owner", unit_address)]
        if timer >= 100 and mode != 3:
            expected.append(("signal", unit_address))
        if label in ("wait", "equal"):
            expected.append(("idle", game_address, unit_address, (7,)))
            expected_timer, low, high = timer, 0x12345678, 666
        else:
            effective = int(radius / 2) if mode == 3 else radius
            low, high = 0x12345678, 666
            draws = []
            for _ in range(2):
                if effective * 2 > 0:
                    stepped = low * 0x6AC690C5 + high
                    low, high = stepped & 0xFFFFFFFF, stepped >> 32
                    draws.append(low % (effective * 2))
                else:
                    draws.append(0)
            x, y = 100 + draws[0] - effective, 90 + draws[1] - effective
            expected.append(("cast", game_address, unit_address, (9, 0x124, 0, x, y)))
            expected_timer = 107
        if calls != expected or (control.scratch_one, unit.seed_low, unit.seed_high) != (expected_timer, low, high):
            raise AssertionError(f"{label}: calls/state differ: {calls!r} != {expected!r}")
    print(f"ok: {len(cases)} pet follow timing and coordinate cases")


if __name__ == "__main__":
    main()
