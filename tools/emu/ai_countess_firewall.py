# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005e5b70's path and reset logic with d2d's Countess think.

--cpp emits the C++ case generator. Skill use is intercepted; synthetic
MonStats and Skills table sizes make the skill valid. See monster-ai.md.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CPP = r'''#include "rules/monsters.hpp"
#include <array>
#include <iostream>
int main() {
    using namespace d2d::rules;
    constexpr std::array<int, 4> frames{0, 700, 701, 1401};
    constexpr std::array<std::pair<int, int>, 2> path{{{111, 222}, {333, 444}}};
    for (int index = 0; index <= 3; ++index)
        for (int frame : frames)
            for (int last : frames) {
                int state = index, state2 = last;
                ThinkIn input; input.state = &state; input.state2 = &state2; input.frame = frame;
                CountessIn where; where.target_away = true; where.at_home = true;
                Rng rng(1);
                const auto act = countess_think(input, where, path, rng);
                std::cout << index << ' ' << frame << ' ' << last << ' ' << (act.act == MonAct::point) << ' '
                          << state << ' ' << state2 << ' ' << act.x << ' ' << act.y << '\n';
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
    game, unit, think, control, path, points, tables, monstats = (machine.alloc(0x1000) for _ in range(8))
    machine.view("ai_think", think).control = control
    machine.view("ai_control", control).preset_record = path
    machine.view("unit", unit).type = 1
    machine.view("unit", unit)["class"] = 0
    machine.w32(path, 2)
    machine.w32(path + 4, points)
    for offset, value in ((4, 111), (8, 222), (16, 333), (20, 444)):
        machine.w32(points + offset, value)
    machine.w32(0x744304, tables)
    machine.w32(tables + 0xA78, monstats)
    machine.w32(tables + 0xA80, 1)
    machine.w32(tables + 0xBA0, 2)
    machine.mu.mem_write(monstats + 0x170, (1).to_bytes(2, "little"))
    casts = []

    def cast(current):
        casts.append((current.arg(3), current.arg(4)))
        return 1

    machine.hook(0x5DEAD0, cast, 5)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 64:
        raise AssertionError(f"expected 64 cases, got {len(rows)}")
    frames = (0, 700, 701, 1401)
    for number, row in enumerate(rows):
        index, frame, last, expected_cast, expected_index, expected_last, x, y = map(int, row.split())
        if (index, frame, last) != (number // 16, frames[number // 4 % 4], frames[number % 4]):
            raise AssertionError(f"case {number}: unexpected input order")
        machine.view("game", game).frame = frame
        machine.view("ai_control", control).scratch_one = index
        machine.view("ai_control", control).scratch_two = last
        casts.clear()
        result = machine.call(0x5E5B70, think, ecx=game, edx=unit, limit=1000)
        actual = (result, machine.view("ai_control", control).scratch_one,
                  machine.view("ai_control", control).scratch_two, casts)
        expected = (expected_cast, expected_index, expected_last, [(x, y)] if expected_cast else [])
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or actual != expected:
            raise AssertionError(f"case {number}: game={actual}, d2d={expected}")
    print(f"ok: {len(rows)} Countess firewall path cases")


if __name__ == "__main__":
    main()
