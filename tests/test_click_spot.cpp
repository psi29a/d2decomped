// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Where a click on the ground walks to, rules::click_spot against game.exe's
// FUN_004623c0 run by its own code (tools/emu/click_spot.py writes the cases):
// every step within 12 subtiles and the 0x100 edges, for each click flag.
#include <click_spot.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>

using namespace d2d::rules;

int main() {
    struct Case { int delta_x, delta_y; std::uint32_t flags; int sent, spot_x, spot_y; };
    constexpr Case cases[]{
#include "click_spot_cases.inc"
    };
    constexpr int kUnitX = 1000, kUnitY = 2000;   // as the oracle's unit stands
    int differ = 0;
    for (const auto& sample : cases) {
        const auto spot = click_spot(kUnitX, kUnitY, kUnitX + sample.delta_x, kUnitY + sample.delta_y, sample.flags);
        const bool same = sample.sent ? spot && spot->x - kUnitX == sample.spot_x && spot->y - kUnitY == sample.spot_y : !spot;
        if (!same) {
            std::printf("differ: step (%d, %d) flags 0x%x: game.exe %d (%d, %d)\n", sample.delta_x, sample.delta_y, unsigned(sample.flags), sample.sent, sample.spot_x, sample.spot_y);
            ++differ;
        }
    }
    std::printf("%zu cases, %d differ\n", sizeof(cases) / sizeof(cases[0]), differ);
    assert(differ == 0);
    // The distance runs short: a step of 2 straight out is 1, so a press there
    // walks 6, not 3.
    assert(approx_distance(2, 0) == 1 && approx_distance(-3, 4) == 5);   // (4 * 0x3d7 + 3 * 0x197) >> 10
    assert(click_least_distance(kClickPress) == 3 && click_least_distance(kClickPress | kClickRun) == 5);
    assert(click_least_distance(kClickHeld) == 4 && click_least_distance(kClickHeld | kClickRun) == 5);
    return differ == 0 ? 0 : 1;
}
