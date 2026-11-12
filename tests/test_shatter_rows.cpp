// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A shattered death's ice (rules::shatter_rows, FUN_004afc10): the rows
// tools/emu/shatter_missiles.py reads from game.exe for every unit type,
// MonStats2 Height and freeze / uberminion mix. No game data needed.
#include <shatter.hpp>

#include <cassert>
#include <cstdio>

int main() {
    using d2d::rules::shatter_rows;
    using d2d::rules::shatter_row_name;
    for (int unit_type : { 0, 2, 3 }) {                       // a monster's only
        const auto none = shatter_rows(unit_type, 2, false, true);
        assert(none.first == -1 && none.second == -1);
    }
    // Height 2 (most act 1 monsters): icebreakmedium, then the smoke.
    const auto medium = shatter_rows(1, 2, false, false);
    assert(medium.first == 0x111 && medium.second == 0x113);
    assert(shatter_row_name(medium.first) == "icebreakmedium" && shatter_row_name(medium.second) == "icebreaksmoke");
    assert(shatter_row_name(shatter_rows(1, 0, false, true).first) == "monglacialspike");
    assert(shatter_row_name(shatter_rows(1, 3, false, true).first) == "icebreaklarge");
    assert(shatter_row_name(0x114).empty() && shatter_row_name(0x10e).empty());
    // Uberminion (184) without freeze: 0x1ab, 0x1aa; with freeze the ice.
    const auto uber = shatter_rows(1, 1, true, false);
    assert(uber.first == 0x1ab && uber.second == 0x1aa);
    assert(shatter_rows(1, 1, true, true).first == 0x110);
    assert(shatter_rows(1, 0xff, false, false).first == 0x10f + 0xff);
    std::printf("OK: shattered deaths as game.exe's\n");
    return 0;
}
