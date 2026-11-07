// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The frame's hover box (FUN_00503000) and the skill tree's place for it
// (FUN_004eef00, 0x4ef3ce..0x4ef3fb): text + 8 wide, its left at x (or x
// less half when centred) clamped into the screen, its bottom y + 2 kept
// between text height - 5 and screen height - 5.
#include <hover_box.hpp>

#include <array>
#include <cassert>
#include <cstdio>

int main() {
    using d2d::rules::hover_box;
    using Box = std::array<int, 4>;
    // The tree: an icon at left 400, bottom 200, a 100 x 32 text: from 24 px
    // left of the icon, 5 px under it.
    assert(d2d::rules::skill_tree_tip_box(400, 200, 100, 32, 800, 600) == (Box{ 376, 205, 484, 237 }));
    assert(hover_box(750, 100, false, 100, 32, 800, 600) == (Box{ 692, 70, 800, 102 }));     // the right edge
    assert(hover_box(-20, 100, false, 100, 32, 800, 600) == (Box{ 0, 70, 108, 102 }));       // the left edge
    assert(hover_box(100, 700, false, 100, 32, 800, 600) == (Box{ 100, 563, 208, 595 }));    // the bottom: screen - 5
    assert(hover_box(100, 0, false, 100, 64, 800, 600) == (Box{ 100, -5, 208, 59 }));        // at least height - 5
    assert(hover_box(400, 100, true, 100, 32, 800, 600) == (Box{ 346, 70, 454, 102 }));      // centred
    std::puts("test_hover_box: ok");
    return 0;
}
