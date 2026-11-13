# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005eeda0's out-of-reach, no-skill branch with d2d.

--cpp emits C++ cases; see monster-ai.md for rerun commands. Search,
distance and successful actions are hooked; the decision and RNG are real.
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
        for (int distance : std::array{19, 20, 30})
            for (int chance : std::array{0, 50, 100}) {
                ThinkIn input; input.ancient = 1; input.target = true; input.picked = true;
                input.reach = false; input.self_size = 2; input.target_size = 2;
                input.target_x = distance + 2; input.aip[0] = 20; input.aip[1] = chance;
                Rng rng(seed); Act5Thinks thinker{{"Ancient", input, rng, away}};
                const auto act = thinker.ancient();
                int action = act.act == MonAct::idle ? 1 : act.act == MonAct::approach ? 2 :
                             act.act == MonAct::a1 ? 4 : -1;
                std::cout << seed << ' ' << distance << ' ' << chance << ' ' << action << ' '
                          << act.n << ' ' << rng.low << ' ' << rng.high << '\n';
            }
}
'''


def main():
    """Emit --cpp cases, or compare --cases against --exe and report mismatches."""
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
    state = {"distance": 0, "action": 0, "value": 0}
    machine.hook(0x58CF90, lambda current: 0, 0)
    machine.hook(0x5E8F20, lambda current: target, 4)
    machine.hook(0x622C40, lambda current: 0, 3)
    machine.hook(0x6416D0, lambda current: state["distance"], 2)

    def stand(current):
        """Hook: record a stand action and its frame count."""
        state["action"], state["value"] = 1, current.arg(0)
        return 0

    def approach(current):
        """Hook: record an approach action and its byte argument."""
        state["action"], state["value"] = 2, current.arg(1) & 0xFF
        return 1

    def attack(current):
        """Hook: record an attack action and its kind."""
        state["action"] = current.arg(0)
        return 1

    machine.hook(0x5DE080, stand, 1)
    machine.hook(0x5DEF80, approach, 2)
    machine.hook(0x5DDF90, attack, 2)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 1152:
        raise AssertionError(f"expected 1152 cases, got {len(rows)}")
    for index, text in enumerate(rows):
        seed, distance, chance, action, value, low, high = map(int, text.split())
        if (seed, distance, chance) != (index // 9, (19, 20, 30)[index // 3 % 3], (0, 50, 100)[index % 3]):
            raise AssertionError(f"case {index}: unexpected input order")
        state.update(distance=distance, action=0, value=0)
        machine.mu.mem_write(row + 0x56, (20).to_bytes(2, "little"))
        machine.mu.mem_write(row + 0x5C, chance.to_bytes(2, "little"))
        unit_view.seed_low, unit_view.seed_high = seed, 666
        machine.call(0x5EEDA0, think, ecx=game, edx=unit, limit=2000)
        actual = (state["action"], state["value"], unit_view.seed_low, unit_view.seed_high)
        expected = (action, value, low, high)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or actual != expected:
            raise AssertionError(f"case {index}: game={actual}, d2d={expected}")
    print(f"ok: {len(rows)} Madawc out-of-reach no-skill cases")


if __name__ == "__main__":
    main()
