# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005e7080's selected path action with d2d's npc_think.

Uses the same C++ case generator as ai_town_path_actions.py. All cases have
one path point and a seed selecting it. Movement and command boundaries are
hooked; game.exe's dispatcher and action functions run for real.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d")
    machine = emu.Emu(str(args.exe))
    unit_address, think_address, control_address, path_address, point_address = (machine.alloc(0x100) for _ in range(5))
    unit = machine.view("unit", unit_address)
    machine.view("ai_think", think_address).control = control_address
    machine.view("ai_control", control_address).preset_record = path_address
    machine.w32(path_address, 1)
    machine.w32(path_address + 4, point_address)
    commands = {4: machine.alloc(0x20), 7: machine.alloc(0x20)}
    state = {"offset": 0, "mode": 0, "walks": 0}
    machine.hook(0x5DC5C0, lambda current: int(state["offset"] != 0), 1)
    machine.hook(0x5DE190, lambda current: 0, 2)

    def walk(current):
        state["walks"] += 1
        return 1

    machine.hook(0x5DEF30, walk, 2)
    machine.hook(0x58EFA0, lambda current: commands[current.arg(0)], 2)
    machine.hook(0x46C140, lambda current: state["mode"], 0)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 170:
        raise AssertionError(f"expected 170 cases, got {len(rows)}")
    for index, row in enumerate(rows):
        action, offset, mode, *expected = map(int, row.split())
        if (action, offset, mode) != (index // 34 + 1, (index // 2) % 17 - 8, index % 2):
            raise AssertionError(f"case {index}: unexpected input order")
        state.update(offset=offset, mode=mode, walks=0)
        unit.seed_low, unit.seed_high = 1, 666
        machine.w32(point_address, action)
        machine.w32(point_address + 4, 100 + offset)
        machine.w32(point_address + 8, 100)
        for address in commands.values():
            machine.mu.mem_write(address, bytes(0x20))
        machine.call(0x5E7080, think_address, ecx=0x1234, edx=unit_address, limit=5000)
        values = []
        for kind in (4, 7):
            values.extend(machine.r32(commands[kind] + offset) for offset in (0x0C, 0x10, 0x14, 0x18))
        actual = [state["walks"], *values]
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or actual != expected:
            raise AssertionError(f"case {index}: game={actual}, d2d={expected}")
    print(f"ok: {len(rows)} town NPC path dispatch cases")


if __name__ == "__main__":
    main()
