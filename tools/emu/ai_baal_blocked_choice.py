# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005fc300's blocked-line choice with the d2d Baal think.

--cpp emits cases. The results are projected to actions because choices 2
and 6 share d2d's action. Life and the target's spell predicate are hooked.
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
            for (int players : std::array{1, 2})
                for (int casting = 0; casting < 2; ++casting)
                    for (int target_x : std::array{0, 90, 120}) {
                        int state = 0, state2 = 0;
                        ThinkIn input; input.state = &state; input.state2 = &state2;
                        input.has_word = true; input.difficulty = difficulty; input.life_pct = 80;
                        input.target_x = target_x; input.target_size = 0; input.players_looked = players;
                        input.reach = false; input.line_clear = false;
                        input.right_skill = casting ? 0x3b : 0; input.skill678[0] = true;
                        Rng rng(seed); Act4Thinks thinker{{"BaalCrab", input, rng, away}};
                        const auto act = thinker.baal_crab();
                        int choice = -1;
                        if (act.act == MonAct::idle) choice = 1;
                        else if (act.act == MonAct::keep) choice = 2;
                        else if (act.act == MonAct::circle) choice = 3;
                        else if (act.act == MonAct::wander) choice = 4;
                        else if (act.act == MonAct::move_to) choice = 5;
                        else if (act.act == MonAct::a2) choice = 10;
                        else if (act.act == MonAct::skill) {
                            constexpr std::array<int, 6> slots{11, 12, 9, 13, 14, 8};
                            if (act.n >= 0 && act.n < int(slots.size())) choice = slots[act.n];
                        }
                        std::cout << seed << ' ' << difficulty << ' ' << players << ' ' << casting << ' '
                                  << target_x << ' ' << choice << '\n';
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
    state = {"casting": 0}
    machine.hook(0x621F20, lambda current: 80, 1)
    machine.hook(0x5FC0C0, lambda current: state["casting"], 0)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 1536:
        raise AssertionError(f"expected 1536 cases, got {len(rows)}")
    for index, text in enumerate(rows):
        seed, difficulty, players, casting, target_x, expected = map(int, text.split())
        if (seed, difficulty, players, casting, target_x) != (
                index // 24, index // 12 % 2, (1, 2)[index // 6 % 2], index // 3 % 2,
                (0, 90, 120)[index % 3]):
            raise AssertionError(f"case {index}: unexpected input order")
        state["casting"] = casting
        machine.view("game", game).difficulty = difficulty
        boss_view.seed_low, boss_view.seed_high = seed, 666
        result = machine.call(0x5FC300, game, target, players, int(target_x == 90 or target_x == 120),
                              int(target_x == 120), regs={"ESI": boss}, limit=1000)
        result = 2 if result == 6 else result
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or result != expected:
            raise AssertionError(f"case {index}: game={result}, d2d={expected}")
    print(f"ok: {len(rows)} Baal blocked-line choice cases")


if __name__ == "__main__":
    main()
