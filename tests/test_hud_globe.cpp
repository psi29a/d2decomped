// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The globes' potion preview and their click (rules/hud_globe.hpp), worked
// by hand from FUN_005485b0 / FUN_00548640's end percents, FUN_00496f80 /
// FUN_00497110's x 0x50 / 100 and FUN_00499500's boxes at 800 x 600.
#include <hud_globe.hpp>

#include <cstdio>
#include <exception>

int main() try {
    using d2d::rules::life_end_percent;
    using d2d::rules::mana_end_percent;
    // 100 of 400 life, a minor healing potion's 30 << 8 over 192 frames
    // (40 a frame), all to come: (7680 + 25600) >> 8 = 130 -> 130 * 100 / 400.
    static_assert(life_end_percent(100 << 8, 400 << 8, 40, 192) == 32);
    static_assert(life_end_percent(100 << 8, 400 << 8, 40, 96) == 28);    // half spent: 115 points
    static_assert(life_end_percent(300 << 8, 200 << 8, 0, 0) == 100);     // 150 %: 100
    static_assert(life_end_percent(100 << 8, 100 << 8, 1000, 128) == 88); // 600 %: its low byte, 88
    static_assert(life_end_percent(100 << 8, 0x80, 40, 192) == 0);        // under one point of max
    // 50 of 200 mana, 100 a frame for 128 frames: (12800 + 12800) * 100 / 51200.
    static_assert(mana_end_percent(50 << 8, 200 << 8, 100, 128) == 50);
    static_assert(mana_end_percent(50 << 8, 0, 100, 128) == 0);
    std::printf("OK: the potions' end percents\n");

    using d2d::rules::globe_clicked;
    using d2d::rules::globe_preview_rows;
    static_assert(globe_preview_rows(0) == 0 && globe_preview_rows(44) == 35 && globe_preview_rows(100) == 80);
    static_assert(globe_preview_rows(110) == 80);       // the gear's regen on top: capped at the cel's 80 rows
    std::printf("OK: the preview's rows\n");

    // y: nothing at or above H-0x30 (552), the boxes' bottom H-0xf (585).
    static_assert(globe_clicked(0x40, 552, 800, 600) == 0 && globe_clicked(0x40, 553, 800, 600) == 1);
    static_assert(globe_clicked(0x40, 585, 800, 600) == 1 && globe_clicked(0x40, 586, 800, 600) == 0);
    // x: life 0x1e..0x6e, mana W-0x6f..W-0x1f (689..769).
    static_assert(globe_clicked(0x1d, 570, 800, 600) == 0 && globe_clicked(0x1e, 570, 800, 600) == 1 && globe_clicked(0x6e, 570, 800, 600) == 1);
    static_assert(globe_clicked(0x6f, 570, 800, 600) == 0 && globe_clicked(688, 570, 800, 600) == 0);
    static_assert(globe_clicked(689, 570, 800, 600) == 2 && globe_clicked(769, 570, 800, 600) == 2 && globe_clicked(770, 570, 800, 600) == 0);
    std::printf("OK: the globes' click boxes\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_hud_globe: %s\n", error.what());
    return 1;
}
