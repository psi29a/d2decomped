// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: font
// A frontend button's label: its font by height (FUN_00500db0, 0x500f7f)
// and where it sits (FUN_00500c50). frontend-menu-table.md "Button labels".
#include <button_label.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    using d2d::font::button_label_font;
    using d2d::font::button_label_spot;

    // The font and the drop by height: 0x23 and up FontExocet10 (4),
    // 0x20 FontRidiculous (3), above 0x14 (2), else (1).
    assert(button_label_font(0x3c).exocet10 && button_label_font(0x3c).drop == 4);
    assert(button_label_font(0x23).exocet10 && button_label_font(0x23).drop == 4);
    assert(!button_label_font(0x22).exocet10 && button_label_font(0x22).drop == 2);
    assert(!button_label_font(0x20).exocet10 && button_label_font(0x20).drop == 3);
    assert(!button_label_font(0x19).exocet10 && button_label_font(0x19).drop == 2);
    assert(!button_label_font(0x15).exocet10 && button_label_font(0x15).drop == 2);
    assert(!button_label_font(0x14).exocet10 && button_label_font(0x14).drop == 1);

    // SINGLE PLAYER (record 0x10: 264, bottom 324, 272 x 35) in FontExocet10
    // (.tbl height 10): 324 - (35 - 10) / 2 + 4 = 316.
    auto spot = button_label_spot(264, 324, 272, 35, 100, 10, false);
    assert(spot.x == 264 + 86 && spot.baseline == 316);
    // Pressed: 2 left, 2 down.
    spot = button_label_spot(264, 324, 272, 35, 100, 10, true);
    assert(spot.x == 262 + 86 && spot.baseline == 318);
    // The gateway (record 0x12: bottom 391, 25 high) in FontRidiculous
    // (height 7): 391 - 9 + 2 = 384.
    assert(button_label_spot(264, 391, 272, 25, 80, 7, false).baseline == 384);
    // A 32-high popup button drops 3: 337 - (32 - 7) / 2 + 3 = 328.
    assert(button_label_spot(281, 337, 96, 32, 40, 7, false).baseline == 328);
    // An odd spare width rounds toward zero; a label wider than the button
    // starts at its left edge.
    assert(button_label_spot(0, 100, 101, 35, 50, 10, false).x == 25);
    assert(button_label_spot(40, 100, 96, 32, 120, 7, false).x == 40);
    assert(button_label_spot(40, 100, 96, 32, 120, 7, true).x == 38);

    // Two lines (flag 0x40) on a 60-high button, font height 10: the
    // first at bottom - 2 * (50 / 3) + 4, the second at bottom - 50 / 3 + 4.
    assert(button_label_spot(0, 500, 168, 60, 50, 10, false, 2, 0).baseline == 500 - 32 + 4);
    assert(button_label_spot(0, 500, 168, 60, 50, 10, false, 2, 1).baseline == 500 - 16 + 4);
    // A negative spare rounds toward zero too: (5 - 10) / 2 is -2.
    assert(button_label_spot(0, 100, 50, 5, 10, 10, false).baseline == 100 + 2 + 1);

    std::puts("button_label: ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_button_label: %s\n", error.what());
    return 1;
}
