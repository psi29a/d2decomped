// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Which states a death moves out of a unit's first state bitmap (rules::states_leave_on_death,
// FUN_00639fb0): second |= ~keep & first, first &= keep, over the cases tools/emu/state_death.py
// reads from game.exe 1.14d (the keep mask picked by unit type and boss flag there).
#include <state_death.hpp>

#include <cstdint>
#include <cstdio>
#include <vector>

using Words = std::vector<std::uint32_t>;

namespace {

struct Case {
    int states, kind, boss;
    Words first, second, player_keep, monster_keep, boss_keep, first_after, second_after;
};

const Case kCases[] = {
#include "state_death_cases.inc"
};

}   // namespace

int main() {
    int failed = 0;
    for (const auto& one : kCases) {
        // FUN_00639fb0: +0x108 for a boss flag, else +0x104 for a monster-side unit, else +0x100 for a player.
        const Words& keep = one.boss ? one.boss_keep : one.kind != 0 ? one.monster_keep : one.player_keep;
        Words first = one.first, second = one.second;
        d2d::rules::states_leave_on_death(first, second, keep);
        if (first != one.first_after || second != one.second_after) ++failed;
    }
    if (failed) { std::fprintf(stderr, "%d of %zu cases differ\n", failed, sizeof(kCases) / sizeof(kCases[0])); return 1; }
    std::printf("OK: %zu death state moves match game.exe (FUN_00639fb0)\n", sizeof(kCases) / sizeof(kCases[0]));
    return 0;
}
