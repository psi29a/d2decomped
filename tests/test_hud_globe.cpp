// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The globes' potion preview (rules/hud_globe.hpp), worked by hand from
// FUN_005485b0 / FUN_00548640's end percents and FUN_00496f80 /
// FUN_00497110's x 0x50 / 100.
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

    using d2d::rules::globe_preview_rows;
    static_assert(globe_preview_rows(0) == 0 && globe_preview_rows(44) == 35 && globe_preview_rows(100) == 80);
    static_assert(globe_preview_rows(110) == 80);       // the gear's regen on top: capped at the cel's 80 rows
    std::printf("OK: the preview's rows\n");

    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_hud_globe: %s\n", error.what());
    return 1;
}
