// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// An aura's next pulse (FUN_0056cda0, FUN_0056cd50): perdelay under 6 is 5,
// and the pulse lands on the first game frame after now that is 1 more than
// a multiple of it, so auras of one perdelay pulse together whenever each
// began. rules::aura_next_frame. Needs no game data.
#include <skills.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    using d2d::rules::aura_next_frame;
    // perdelay 50, the paladin auras': frames 1, 51, 101, ...
    assert(aura_next_frame(1, 50) == 51);
    assert(aura_next_frame(2, 50) == 51);
    assert(aura_next_frame(50, 50) == 51);
    assert(aura_next_frame(51, 50) == 101);    // a pulse at 51 arms the next at 101, never 51 again
    assert(aura_next_frame(77, 50) == 101);
    // A blank perdelay (the druid spirits' auras, the masteries) is 0: 5 frames.
    assert(aura_next_frame(1, 0) == 6 && aura_next_frame(6, 0) == 11 && aura_next_frame(9, 0) == 11);
    assert(aura_next_frame(9, 5) == 11 && aura_next_frame(9, 3) == 11);
    assert(aura_next_frame(9, 6) == 13);       // 6 is kept
    // Two auras that began apart pulse on the same frames.
    int first = 3, second = 40;
    for (int step = 0; step < 4; ++step) { first = aura_next_frame(first, 50); second = aura_next_frame(second, 50); }
    assert(first == second && first == 201);
    std::puts("OK: an aura's next pulse (FUN_0056cda0)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
