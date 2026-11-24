// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A monster's own unit alignment from its MonStats Align (rules::unit_alignment), what make_monster
// gives it and Fight::monster_states puts back each frame before a curse's: good (1) is 2, neutral
// (2) is 1, evil (0) is 0. The good one is no friend of the evil one's (rules::friends).
#include <monsters.hpp>

#include <cstdio>

using namespace d2d::rules;

static_assert(unit_alignment(1) == 2 && unit_alignment(2) == 1 && unit_alignment(0) == 0);

int main() {
    if (friends(unit_alignment(1), unit_alignment(0)) || !friends(unit_alignment(1), 2) || !friends(unit_alignment(0), 0)) return 1;
    std::puts("OK: a monster's own alignment by its MonStats Align");
    return 0;
}
