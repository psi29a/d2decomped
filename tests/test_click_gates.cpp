// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// What lets a held button or a warp's click go out: rules::held_walk_goes,
// held_frame_goes, warp_click_goes and kWarpReach against game.exe's own
// FUN_004623c0 throttle, FUN_00462930 and FUN_00461dc0's warp
// (tools/emu/click_gates.py writes the cases).
#include <click_spot.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

using namespace d2d::rules;

int main() try {
    struct Case { long long kind, a, b, c, d, result, after; };
    constexpr Case cases[]{
#include "click_gates_cases.inc"
    };
    int differ = 0;
    for (const auto& sample : cases) {
        long long result = 0, after = 0;
        if (sample.kind == 0) {          // flags, mode, clock, stamp: what goes stamps the clock
            const bool goes = held_walk_goes(std::uint32_t(sample.a), int(sample.b), std::uint32_t(sample.c), std::uint32_t(sample.d));
            result = goes ? 1 : 0;
            after = goes ? sample.c : sample.d;
        } else if (sample.kind == 1) {   // unit type, player mode: a held frame leaves the arm as it was
            result = held_frame_goes(int(sample.a), int(sample.b)) ? 1 : 0;
            after = 1;
        } else {                         // now, until, distance: at once in reach, else walk and queue; 500 ms on
            const bool goes = warp_click_goes(std::uint32_t(sample.a), std::uint32_t(sample.b));
            result = !goes ? 0 : sample.c < kWarpReach ? 1 : 2;
            after = goes ? (std::uint32_t(sample.a) + kWarpClickWaitMs) : std::uint32_t(sample.b);
        }
        if (result != sample.result || after != sample.after) {
            std::printf("differ: kind %lld (%lld, %lld, %lld, %lld): game.exe %lld %lld, d2d %lld %lld\n", sample.kind, sample.a, sample.b, sample.c, sample.d,
                        sample.result, sample.after, result, after);
            ++differ;
        }
    }
    std::printf("%zu cases, %d differ\n", sizeof(cases) / sizeof(cases[0]), differ);
    assert(differ == 0);
    return differ == 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_click_gates: %s\n", error.what());
    return 1;
}
