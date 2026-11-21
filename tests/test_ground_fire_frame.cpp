// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A ground fire's frame (client do 5: FUN_004d3540 picks, FUN_004d30c0 -> FUN_00621330 / FUN_00621310 steps), on
// Missiles.txt's firewall row: AnimLen 37, SubLoop 1, SubStart 12, SubStop 36, AnimSpeed 16 (256ths a frame), Range 90.
// It rises 0..11, jumps to 12 + rand(24), burns in 12..35 (never back to 0), then from 12 frames left sinks a frame a
// tick to 1. The expected frames are worked by hand from those rules.

#include <missiles.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <set>

int main() try {
    constexpr int kLen = 37, kStart = 12, kStop = 36, kRate = 256, kLife = 90;
    const auto frame_at = [](int ticks, std::uint32_t seed_value) {
        d2d::rules::Rng seed{ seed_value };
        return d2d::rules::ground_fire_frame(ticks, kLife, kLen, kStart, kStop, true, kRate, seed);
    };
    std::set<int> jumped;
    for (std::uint32_t seed_value = 1; seed_value <= 64; ++seed_value) {
        for (int ticks = 0; ticks <= 11; ++ticks) assert(frame_at(ticks, seed_value) == ticks);   // the rise
        for (int ticks = 12; ticks <= 78; ++ticks) {                                               // the burn, 12+ frames left
            const int frame = frame_at(ticks, seed_value);
            assert(frame >= kStart && frame < kStop);
        }
        jumped.insert(frame_at(12, seed_value));
        assert(frame_at(79, seed_value) == 10);                                                    // 12 left: SubStart - 3, then a step
        for (int sink = 1; sink <= 9; ++sink) assert(frame_at(79 + sink, seed_value) == 10 - sink);   // two back, one on
        assert(frame_at(89, seed_value) == 1 && frame_at(90, seed_value) == 1);                    // held at 0, then a step
    }
    assert(jumped.size() > 8);   // the jump is a draw, not one frame
    std::printf("OK: ground fire frames (FUN_004d3540), %zu jump frames seen\n", jumped.size());
    return 0;
} catch (const std::exception& error) {
    std::printf("test_ground_fire_frame: %s\n", error.what());
    return 1;
}
