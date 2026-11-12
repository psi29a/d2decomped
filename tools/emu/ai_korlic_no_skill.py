# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005ef010's no-Skill1 branch with d2d's Ancient think.

--cpp emits C++ cases; see monster-ai.md for rerun commands. The search,
distance and actions are hooked; game.exe's decision and RNG run for real.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CPP = r'''#include "rules/thinks/act5.hpp"
#include <array>
#include <iostream>
int main() {
    using namespace d2d::rules;
    ThinkAway away = [](int, bool) { return false; };
    for (unsigned seed = 0; seed < 128; ++seed)
        for (int reach = 0; reach < 2; ++reach)
            for (int chance : std::array{0, 30, 50, 100}) {
                ThinkIn input; input.ancient = 2; input.target = true; input.picked = true;
                input.reach = reach; input.aip[2] = chance;
                Rng rng(seed); Act5Thinks thinker{{"Ancient", input, rng, away}};
                const auto act = thinker.ancient();
                int action = act.act == MonAct::idle ? 1 : act.act == MonAct::direct ? 2 :
                             act.act == MonAct::a1 ? 4 : act.act == MonAct::a2 ? 5 : -1;
                std::cout << seed << ' ' << reach << ' ' << chance << ' ' << action << ' '
                          << (action == 1 ? act.n : act.wait) << ' ' << rng.low << ' ' << rng.high << '\n';
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
    game, unit, think, row, target = (machine.alloc(0x200) for _ in range(5))
    machine.view("ai_think", think).monstats = row
    unit_view = machine.view("unit", unit)
    state = {"reach": 0, "action": 0, "wait": 0}
    machine.hook(0x58CF90, lambda current: 0, 0)
    machine.hook(0x5E8F20, lambda current: target, 4)
    machine.hook(0x622C40, lambda current: state["reach"], 3)
    machine.hook(0x6416D0, lambda current: 10, 2)

    def stand(current):
        state["action"], state["wait"] = 1, current.arg(0)
        return 0

    def direct(current):
        state["action"] = 2
        return 1

    def wait(current):
        state["wait"] = current.arg(0)
        return 0

    def attack(current):
        state["action"] = current.arg(0)
        return 1

    machine.hook(0x5DE080, stand, 1)
    machine.hook(0x5DECA0, direct, 1)
    machine.hook(0x5DE0F0, wait, 1)
    machine.hook(0x5DDF90, attack, 2)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 1024:
        raise AssertionError(f"expected 1024 cases, got {len(rows)}")
    for index, text in enumerate(rows):
        seed, reach, chance, action, delay, low, high = map(int, text.split())
        if (seed, reach, chance) != (index // 8, index // 4 % 2, (0, 30, 50, 100)[index % 4]):
            raise AssertionError(f"case {index}: unexpected input order")
        state.update(reach=reach, action=0, wait=0)
        machine.mu.mem_write(row + 0x62, chance.to_bytes(2, "little"))
        unit_view.seed_low, unit_view.seed_high = seed, 666
        machine.call(0x5EF010, think, ecx=game, edx=unit, limit=2000)
        actual = (state["action"], state["wait"], unit_view.seed_low, unit_view.seed_high)
        expected = (action, delay, low, high)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or actual != expected:
            raise AssertionError(f"case {index}: game={actual}, d2d={expected}")
    print(f"ok: {len(rows)} Korlic no-skill decision cases")


if __name__ == "__main__":
    main()
