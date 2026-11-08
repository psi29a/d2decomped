// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The view with side panels open (rules/panel_view.hpp): FUN_00455f20's
// states (FUN_0045aea0's argument) and the quarter-screen shift
// (0x7a5214) that puts the player in the middle of the free half.
#include <panel_view.hpp>

#include <cassert>
#include <cstdio>

int main() {
    using namespace d2d::rules;
    // FUN_00455f20: the inventory or tree alone 1, a left panel alone 2,
    // both 3; a store, the stash or the cube 3 whatever else is open.
    static_assert(panel_view_state(false, false, false) == panel_view::kWhole);
    static_assert(panel_view_state(false, true, false) == panel_view::kRightOpen);
    static_assert(panel_view_state(true, false, false) == panel_view::kLeftOpen);
    static_assert(panel_view_state(true, true, false) == panel_view::kCovered);
    static_assert(panel_view_state(false, true, true) == panel_view::kCovered);
    static_assert(panel_view_state(false, false, true) == panel_view::kCovered);
    std::printf("OK: the states\n");

    // 0x7a5214 = -W/4 (1), +W/4 (2), else 0; the world draws that much left
    // of centre negated: the player at x 200 with the inventory open, 600
    // with the character panel, at 800 x 600 (and 160 / 480 at 640).
    constexpr int kWidth = 800;
    static_assert(kWidth / 2 - panel_view_shift(panel_view::kRightOpen, kWidth) == 200);
    static_assert(kWidth / 2 - panel_view_shift(panel_view::kLeftOpen, kWidth) == 600);
    static_assert(panel_view_shift(panel_view::kWhole, kWidth) == 0 && panel_view_shift(panel_view::kCovered, kWidth) == 0);
    static_assert(320 - panel_view_shift(panel_view::kRightOpen, 640) == 160 && 320 - panel_view_shift(panel_view::kLeftOpen, 640) == 480);
    std::printf("OK: the player in the middle of the free half\n");
    return 0;
}
