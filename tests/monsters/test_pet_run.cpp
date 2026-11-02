// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A Shadow's run (FUN_00624390 -> FUN_00620e80): the player's run list on
// its class WalkVelocity, an integer percent.
#include <pets.hpp>

#include <cassert>
#include <cstdio>

using namespace d2d::rules;

int main() {
    assert(run_percent(6, 9) == 50);                 // the Assassin: 6 x (100 + 50) / 100 = 9
    assert(run_percent(6, 9) * 6 / 100 + 6 == 9);
    assert(run_percent(7, 9) == 28);                 // 9 x 100 / 7 = 128: truncated
    assert(run_percent(0, 9) == 0);
    std::puts("test_pet_run: ok");
}
