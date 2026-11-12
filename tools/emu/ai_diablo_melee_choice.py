# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005e8810's melee choice with d2d's Diablo think.

--cpp emits C++ cases. Search, life, state, resistances and line-of-sight
are hooked; game.exe's chooser and its seed update run for real.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CPP = r'''#include "rules/thinks/act4.hpp"
#include <array>
#include <iostream>
int main() {
    using namespace d2d::rules;
    ThinkAway away = [](int, bool) { return false; };
    for (unsigned seed = 0; seed < 64; ++seed)
        for (int line = 0; line < 2; ++line)
            for (int life : std::array{19, 20})
                for (int state_flag = 0; state_flag < 2; ++state_flag)
                    for (int resistance : std::array{-1, 0, 1}) {
                        int state = 0;
                        ThinkIn input; input.state = &state; input.has_word = true;
                        input.reach = true; input.line_clear = line; input.target_life_pct = life;
                        input.target_state_b = state_flag; input.target_fire_res = resistance;
                        input.target_light_res = 0; input.usable = true;
                        input.skill = {true, true, true}; input.skill45 = {true, true};
                        input.skill678 = {true, true, false};
                        Rng rng(seed); Act4Thinks thinker{{"Diablo", input, rng, away}};
                        const auto act = thinker.diablo();
                        int choice = -1;
                        if (act.act == MonAct::a1) choice = 2;
                        else if (act.act == MonAct::a2) choice = 3;
                        else if (act.act == MonAct::skill) {
                            constexpr std::array<int, 4> slots{5, 7, 6, 8};
                            if (act.n >= 0 && act.n < int(slots.size())) choice = slots[act.n];
                        }
                        std::cout << seed << ' ' << line << ' ' << life << ' ' << state_flag << ' '
                                  << resistance << ' ' << choice << ' ' << rng.low << ' ' << rng.high << '\n';
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
    game, boss, target, control = (machine.alloc(0x100) for _ in range(4))
    machine.view("unit", target).type = 1
    boss_view = machine.view("unit", boss)
    state = {"line": 0, "life": 0, "state": 0, "resistance": 0}
    machine.hook(0x554010, lambda current: game, 0)
    machine.hook(0x620190, lambda current: 0, 1)
    machine.hook(0x6201D0, lambda current: 0, 1)
    machine.hook(0x622C40, lambda current: 1, 3)
    machine.hook(0x5DD290, lambda current: state["line"], 0)
    machine.hook(0x625480, lambda current: state["resistance"] & 0xFFFFFFFF if current.arg(1) == 0x27 else 0, 3)
    machine.hook(0x621F20, lambda current: state["life"], 1)
    machine.hook(0x639DF0, lambda current: state["state"], 2)
    machine.hook(0x5FD470, lambda current: 1, 4)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 1536:
        raise AssertionError(f"expected 1536 cases, got {len(rows)}")
    for index, text in enumerate(rows):
        seed, line, life, state_flag, resistance, choice, low, high = map(int, text.split())
        if (seed, line, life, state_flag, resistance) != (
                index // 24, index // 12 % 2, (19, 20)[index // 6 % 2], index // 3 % 2,
                (-1, 0, 1)[index % 3]):
            raise AssertionError(f"case {index}: unexpected input order")
        state.update(line=line, life=life, state=state_flag, resistance=resistance)
        machine.view("ai_control", control).scratch_one = 0
        boss_view.seed_low, boss_view.seed_high = seed, 666
        result = machine.call(0x5E8810, target, 0, 1, 0, ecx=control, edx=boss, limit=5000)
        actual = (result, boss_view.seed_low, boss_view.seed_high)
        expected = (choice, low, high)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or actual != expected:
            raise AssertionError(f"case {index}: game={actual}, d2d={expected}")
    print(f"ok: {len(rows)} Diablo melee-choice cases")


if __name__ == "__main__":
    main()
