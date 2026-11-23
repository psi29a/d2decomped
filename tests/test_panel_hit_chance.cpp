// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// rules::panel_hit_chance: FUN_004a7340's arithmetic (tools/emu/hit_chance.py's model, run against
// game.exe there) and FUN_004a74a0's, the same with the roles swapped: a negative defence moves to the
// rating, a negative rating to the defence, both 0 is 100 %, rating x 100 / (rating + defence), then x 2
// x the attacker's level / both levels (idiv), held to 5..95.

#include <combat.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    using d2d::rules::panel_hit_chance;
    assert(panel_hit_chance(100, 100, 10, 10) == 50);                // 50 %, levels even
    assert(panel_hit_chance(300, 100, 20, 10) == 95);                // 75 x 40 / 30 = 100: held to 95
    assert(panel_hit_chance(10, 1000, 1, 30) == 5);                  // 0 after the levels: held to 5
    assert(panel_hit_chance(100, -50, 10, 10) == 95);                // the defence's -50 moved to the rating: 150 vs 0, 100 %
    assert(panel_hit_chance(40, -40, 1, 3) == 50);                   // 80 vs 0: 100 % x 2 x 1 / 4
    assert(panel_hit_chance(-20, 80, 10, 10) == 5);                  // the rating's -20 moved to the defence: 0 vs 100
    assert(panel_hit_chance(0, 0, 10, 10) == 95);                    // both 0: 100 %, then x 1: held
    assert(panel_hit_chance(50, 150, 30, 10) == 25 * 60 / 40);       // 25 % x 2 x 30 / 40 = 37
    std::puts("OK: panel_hit_chance");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_panel_hit_chance: %s\n", error.what());
    return 1;
}
