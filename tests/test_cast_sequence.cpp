// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A cast sequence (anim SQ, seqtrans SC: Lightning / Chain Lightning, sequence 12):
// its 19 steps run out at the cast speed, ceil(256 x steps / speed) ticks with no
// step skipped (FUN_00621260's countdown, FUN_00623e00's step), the hit on step 7.
#include <combat.hpp>
#include <sequences.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

int main() try {
    using namespace d2d::rules;
    const auto steps = sequence(12, "hth");
    assert(steps.size() == 19);
    const auto hit = std::ranges::find(steps, std::uint8_t{ 1 }, &SeqFrame::event);
    assert(hit - steps.begin() == 7);
    // The Sorceress's SC speed 256: the published Lightning table, 19 ticks down to 11.
    const int breakpoints[][2] = { { 0, 19 }, { 7, 18 }, { 15, 17 }, { 23, 16 }, { 35, 15 }, { 52, 14 }, { 78, 13 }, { 117, 12 }, { 194, 11 } };
    for (const auto& [fcr, ticks] : breakpoints) {
        assert(cast_sequence_ticks(19, 256, fcr) == ticks);
        if (fcr > 0) assert(cast_sequence_ticks(19, 256, fcr - 1) == ticks + 1);
    }
    assert(cast_sequence_ticks(0, 256, 0) == 0);
    assert(cast_action_ticks(7, 256, 0) == 7);
    std::puts("cast sequence ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
