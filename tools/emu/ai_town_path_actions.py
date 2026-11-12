# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare town path actions with d2d's npc_think.

FUN_005e6de0, FUN_005e6e80, FUN_005e6ef0, FUN_005e6f00 and FUN_005e6fc0.
--cpp emits the C++ case generator; see town-npcs.md for rerun commands.
Selection of a path point is held fixed. Movement and command allocation are
hooked, while game.exe writes the command fields itself.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
ADDRESSES = (0x5E6DE0, 0x5E6E80, 0x5E6EF0, 0x5E6F00, 0x5E6FC0)
CPP = r'''#include "rules/town_npcs.hpp"
#include <array>
#include <iostream>
int main() {
    using namespace d2d::rules;
    for (int action = 1; action <= 5; ++action)
        for (int offset = -8; offset <= 8; ++offset)
            for (int mode = 0; mode < 2; ++mode) {
                NpcBrain brain; brain.homed = true;
                Rng seed(1);
                std::array<NpcPoint, 1> path{{{action, 100 + offset, 100}}};
                const unsigned modes = mode ? (1u << 8) | (1u << 9) : 0;
                const auto act = npc_think(brain, seed, path, 100, 100, 0, modes, 0, {}, true);
                std::cout << action << ' ' << offset << ' ' << mode << ' '
                          << (act.kind == NpcAct::Kind::walk) << ' '
                          << brain.linger_x << ' ' << brain.linger_y << ' '
                          << brain.linger_count << ' ' << brain.linger_stand << ' '
                          << brain.special_mode << ' ' << brain.special_x << ' '
                          << brain.special_y << ' ' << brain.special_tries << '\n';
            }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpp", action="store_true")
    parser.add_argument("--cases", type=Path)
    parser.add_argument("--exe", type=Path)
    args = parser.parse_args()
    if args.cpp:
        print(CPP, end="")
        return
    if not args.exe or not args.cases:
        parser.error("--cases and --exe are required unless --cpp is used")
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d")
    machine = emu.Emu(str(args.exe))
    unit = machine.alloc(0x100)
    point = machine.alloc(0x20)
    commands = {4: machine.alloc(0x20), 7: machine.alloc(0x20)}
    state = {"offset": 0, "mode": 0, "walks": 0, "allocated": set()}
    machine.hook(0x5DC5C0, lambda current: int(state["offset"] != 0), 1)
    machine.hook(0x5DE190, lambda current: 0, 2)

    def walk(current):
        state["walks"] += 1
        return 1

    def command(current):
        kind = current.arg(0)
        if kind not in commands:
            raise AssertionError(f"unexpected command {kind}")
        state["allocated"].add(kind)
        return commands[kind]

    machine.hook(0x5DEF30, walk, 2)
    machine.hook(0x58EFA0, command, 2)
    machine.hook(0x46C140, lambda current: state["mode"], 0)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 170:
        raise AssertionError(f"expected 170 cases, got {len(rows)}")
    for index, row in enumerate(rows):
        action, offset, mode, *expected = map(int, row.split())
        if (action, offset, mode) != (index // 34 + 1, (index // 2) % 17 - 8, index % 2):
            raise AssertionError(f"case {index}: unexpected input order")
        state.update(offset=offset, mode=mode, walks=0, allocated=set())
        machine.w32(point + 4, 100 + offset)
        machine.w32(point + 8, 100)
        for address in commands.values():
            machine.mu.mem_write(address, bytes(0x20))
        machine.call(ADDRESSES[action - 1], 0, point, ecx=0x1234, edx=unit, limit=5000)
        values = []
        for kind in (4, 7):
            address = commands[kind]
            values.extend(machine.r32(address + offset) for offset in (0x0C, 0x10, 0x14, 0x18))
        actual = [state["walks"], *values]
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or actual != expected:
            raise AssertionError(f"case {index}: game={actual}, d2d={expected}")
    print(f"ok: {len(rows)} town NPC path-action cases")


if __name__ == "__main__":
    main()
