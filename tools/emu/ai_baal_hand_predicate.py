# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005fc070 and FUN_005fc0c0 through Baal's blocked-line choice.

--cpp emits C++ cases. Unit-skill lookup, skill IDs and levels are hooked;
the original hand predicates and blocked-line chooser execute for real.
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
    constexpr std::array<int, 6> ids{0, 0x1b, 0x33, 0x38, 0x3b, 0x40};
    constexpr std::array<int, 5> levels{0, 3, 4, 7, 8};
    for (unsigned seed = 0; seed < 64; ++seed)
        for (int hand = 0; hand < 2; ++hand)
            for (int id : ids)
                for (int level : levels) {
                    int state = 0, state2 = 0;
                    ThinkIn input; input.state = &state; input.state2 = &state2;
                    input.has_word = true; input.life_pct = 80; input.players_looked = 1;
                    input.reach = false; input.line_clear = false; input.skill678[0] = true;
                    if (hand == 0) { input.right_skill = id; input.right_level = level; }
                    else { input.left_skill = id; input.left_level = level; }
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
                    std::cout << seed << ' ' << hand << ' ' << id << ' ' << level << ' ' << choice << '\n';
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
    game, boss, target = (machine.alloc(0x100) for _ in range(3))
    boss_view = machine.view("unit", boss)
    state = {"hand": 0, "id": 0, "level": 0}
    machine.hook(0x621F20, lambda current: 80, 1)
    machine.hook(0x6201D0, lambda current: int(state["hand"] == 0 and state["id"] != 0), 1)
    machine.hook(0x620190, lambda current: int(state["hand"] == 1 and state["id"] != 0), 1)
    machine.hook(0x643CE0, lambda current: state["id"], 3)
    machine.hook(0x6442A0, lambda current: state["level"], 3)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    ids, levels = (0, 0x1B, 0x33, 0x38, 0x3B, 0x40), (0, 3, 4, 7, 8)
    if len(rows) != 3840:
        raise AssertionError(f"expected 3840 cases, got {len(rows)}")
    for index, text in enumerate(rows):
        seed, hand, skill_id, level, expected = map(int, text.split())
        if (seed, hand, skill_id, level) != (index // 60, index // 30 % 2, ids[index // 5 % 6], levels[index % 5]):
            raise AssertionError(f"case {index}: unexpected input order")
        state.update(hand=hand, id=skill_id, level=level)
        boss_view.seed_low, boss_view.seed_high = seed, 666
        result = machine.call(0x5FC300, game, target, 1, 0, 0, regs={"ESI": boss}, limit=2000)
        result = 2 if result == 6 else result
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or result != expected:
            raise AssertionError(f"case {index}: game={result}, d2d={expected}")
    print(f"ok: {len(rows)} Baal hand-predicate choice cases")


if __name__ == "__main__":
    main()
