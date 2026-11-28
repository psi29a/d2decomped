// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A level region's pick (FUN_005bde80) walks its own count (+0x10, Region::spawns), not the rows FUN_00547bc0
// joins after them (+0x12, rarity 0): a region of joined rows alone picks none (-1), and no pick of a populated
// one reaches a joined row (CodeRabbit on #815). No game data.

#include <monsters.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>

int main() {
    d2d::rules::Region joined_only;
    joined_only.types = { { 77, 0 } };                            // a row region_sets joined: rarity 0, not a spawn
    d2d::rules::Rng seed{ 9 };
    assert(d2d::rules::pick_type(joined_only, seed) == -1);
    d2d::rules::Region region;
    region.types = { { 5, 3 }, { 6, 1 }, { 77, 0 } };
    region.total = 4; region.spawns = 2;
    int fives = 0;
    for (int k = 0; k < 2000; ++k) {
        const int type = d2d::rules::pick_type(region, seed);
        assert(type == 5 || type == 6);
        fives += type == 5;
    }
    assert(fives > 1000);                                          // 3 in 4
    std::puts("test_region_pick: ok");
    return 0;
}
