// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The facing a shift-click's search starts from (FUN_006238c0): rules::search_facing
// against game.exe's own run of it (tools/emu/search_facing.py inc writes the cases).
#include <search_facing.hpp>

#include <cassert>
#include <cstdio>

using namespace d2d::rules;

int main() {
    struct Case { int x, y, target_x, target_y, new_dir, facing; };
    static const Case cases[]{
#include "search_facing_cases.inc"
    };
    int differ = 0;
    for (const auto& sample : cases) {
        const int got = search_facing(sample.x, sample.y, sample.target_x, sample.target_y, sample.new_dir);
        if (got != sample.facing) {
            std::printf("differ: (%d, %d) to (%d, %d), +0x65 %d: game.exe %d, d2d %d\n", sample.x, sample.y, sample.target_x, sample.target_y, sample.new_dir, sample.facing, got);
            ++differ;
        }
    }
    std::printf("%zu cases, %d differ\n", sizeof(cases) / sizeof(cases[0]), differ);
    assert(differ == 0);
    // Off its target the path's +0x65 doesn't count: 5 subtiles south-east of the unit is direction 0 whatever it holds.
    assert(search_facing(100, 100, 105, 105, 17) == search_facing(100, 100, 105, 105, 40));
    // On it, only +0x65 does.
    assert(search_facing(100, 100, 100, 100, 17) == 17);
    return differ == 0 ? 0 : 1;
}
