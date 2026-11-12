# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005e6ae0's active-linger branch with d2d's npc_think.

--cpp emits a C++ case generator; see town-npcs.md for rerun commands.
The command lookup, distance and movement boundaries are hooked.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CPP = r'''#include "rules/town_npcs.hpp"
#include <iostream>
int main() {
    using namespace d2d::rules;
    for (int offset = -8; offset <= 8; ++offset)
        for (int count = 1; count <= 5; ++count) {
            NpcBrain brain; brain.homed = true; brain.linger_x = 100 + offset; brain.linger_y = 100;
            brain.linger_count = count; brain.linger_stand = 10;
            Rng seed(1);
            const auto act = npc_think(brain, seed, {}, 100, 100, 0, 0, 0, {}, true);
            std::cout << offset << ' ' << count << ' ' << (act.kind == NpcAct::Kind::walk) << ' '
                      << act.frames << ' ' << brain.linger_count << '\n';
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
    unit, command = machine.alloc(0x100), machine.alloc(0x20)
    state = {"offset": 0, "walks": 0, "frames": 0}
    machine.hook(0x58EEF0, lambda current: command, 1)
    machine.hook(0x5DC5C0, lambda current: abs(state["offset"]), 1)
    machine.hook(0x5DE190, lambda current: 0, 2)

    def walk(current):
        state["walks"] += 1
        return 1

    def stand(current):
        state["frames"] = current.arg(0)
        return 0

    machine.hook(0x5DEF30, walk, 2)
    machine.hook(0x5DE080, stand, 1)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 85:
        raise AssertionError(f"expected 85 cases, got {len(rows)}")
    for index, row in enumerate(rows):
        offset, count, *expected = map(int, row.split())
        if (offset, count) != (index // 5 - 8, index % 5 + 1):
            raise AssertionError(f"case {index}: unexpected input order")
        state.update(offset=offset, walks=0, frames=0)
        for field, value in ((0x0C, 100 + offset), (0x10, 100), (0x14, count), (0x18, 10)):
            machine.w32(command + field, value)
        machine.call(0x5E6AE0, ecx=0x1234, edx=unit, limit=1000)
        actual = [state["walks"], state["frames"], machine.r32(command + 0x14)]
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or actual != expected:
            raise AssertionError(f"case {index}: game={actual}, d2d={expected}")
    print(f"ok: {len(rows)} town NPC active-linger cases")


if __name__ == "__main__":
    main()
