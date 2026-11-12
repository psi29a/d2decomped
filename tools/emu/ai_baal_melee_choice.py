# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005fc1c0's melee choice with d2d's Baal think.

--cpp emits C++ cases. Life percentages and target state are hooked; action
identities and the post-choice seed are compared. See monster-ai.md.
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
        for (int difficulty = 0; difficulty < 2; ++difficulty)
            for (int life : std::array{20, 100})
                for (int target_life : std::array{32, 33})
                    for (int state_flag = 0; state_flag < 2; ++state_flag)
                        for (int line = 0; line < 2; ++line) {
                            int state = 0, state2 = 0;
                            ThinkIn input; input.state = &state; input.state2 = &state2; input.reach = true;
                            input.has_word = true; input.difficulty = difficulty; input.life_pct = life;
                            input.target_life_pct = target_life; input.target_state_b = state_flag;
                            input.line_clear = line; input.skill678[0] = true;
                            Rng rng(seed); Act4Thinks thinker{{"BaalCrab", input, rng, away}};
                            const auto act = thinker.baal_crab();
                            int choice = -1;
                            if (act.act == MonAct::idle) choice = 1;
                            else if (act.act == MonAct::a2) choice = 10;
                            else if (act.act == MonAct::skill) {
                                constexpr std::array<int, 6> slots{11, 12, 9, 13, 14, 8};
                                if (act.n >= 0 && act.n < int(slots.size())) choice = slots[act.n];
                            }
                            std::cout << seed << ' ' << difficulty << ' ' << life << ' ' << target_life << ' '
                                      << state_flag << ' ' << line << ' ' << choice << ' ' << rng.low << ' ' << rng.high << '\n';
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
    game, boss, target = (machine.alloc(0x100) for _ in range(3))
    boss_view = machine.view("unit", boss)
    state = {"life": 0, "target_life": 0, "target_state": 0}
    machine.hook(0x621F20, lambda current: state["life"] if current.arg(0) == boss else state["target_life"], 1)
    machine.hook(0x639DF0, lambda current: state["target_state"], 2)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 2048:
        raise AssertionError(f"expected 2048 cases, got {len(rows)}")
    for index, text in enumerate(rows):
        seed, difficulty, life, target_life, state_flag, line, choice, low, high = map(int, text.split())
        if (seed, difficulty, life, target_life, state_flag, line) != (
                index // 32, index // 16 % 2, (20, 100)[index // 8 % 2],
                (32, 33)[index // 4 % 2], index // 2 % 2, index % 2):
            raise AssertionError(f"case {index}: unexpected input order")
        state.update(life=life, target_life=target_life, target_state=state_flag)
        machine.view("game", game).difficulty = difficulty
        boss_view.seed_low, boss_view.seed_high = seed, 666
        result = machine.call(0x5FC1C0, game, target, line, regs={"EAX": boss}, limit=1000)
        actual = (result, boss_view.seed_low, boss_view.seed_high)
        expected = (choice, low, high)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or actual != expected:
            raise AssertionError(f"case {index}: game={actual}, d2d={expected}")
    print(f"ok: {len(rows)} Baal melee-choice cases")


if __name__ == "__main__":
    main()
