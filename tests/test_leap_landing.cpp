// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Leap's landing (FUN_005d9a10, 0x5d9a10), in subtiles: the point pulled
// back to the range by the AI distance, then the point and a step either
// side across the way, each its nearest free spot, a clear line to it and
// within range; the first that passes.
#include <leap.hpp>

#include <cassert>
#include <cstdio>
#include <optional>
#include <utility>

int main() {
    using Spot = std::optional<std::pair<int, int>>;
    const auto as_is = [](int at_x, int at_y) -> Spot { return std::pair{ at_x, at_y }; };
    const auto open = [](int, int) { return true; };
    // (30, 10) at range 10: d = (10 + 60) / 2 = 35, so (300 / 35, 100 / 35) = (8, 2).
    assert((d2d::rules::leap_landing(0, 0, 30, 10, 10, as_is, open) == Spot{ { 8, 2 } }));
    assert((d2d::rules::leap_landing(0, 0, 4, 3, 10, as_is, open) == Spot{ { 4, 3 } }));   // in range: as clicked
    // The point's line blocked: the first side step (x + sx, y + sy), sx = y - cy = 2 -> 2, sy = cx - x = -8 -> -2.
    const auto not_point = [](int at_x, int at_y) { return !(at_x == 8 && at_y == 2); };
    assert((d2d::rules::leap_landing(0, 0, 30, 10, 10, as_is, not_point) == Spot{ { 10, 0 } }));
    // That one too: the other side (x - sx, y - sy).
    const auto only_third = [](int at_x, int at_y) { return at_x == 6 && at_y == 4; };
    assert((d2d::rules::leap_landing(0, 0, 30, 10, 10, as_is, only_third) == Spot{ { 6, 4 } }));
    // A free spot found past the range fails that try; none at all, no leap.
    const auto too_far = [](int, int) -> Spot { return std::pair{ 40, 0 }; };
    assert(!d2d::rules::leap_landing(0, 0, 30, 10, 10, too_far, open));
    assert(!d2d::rules::leap_landing(0, 0, 30, 10, 10, [](int, int) -> Spot { return std::nullopt; }, open));
    // The side step held to -2..2 past -1 / 1, kept within -1..1.
    // (5, 1): sx = 1 (kept), sy = -5 -> -2; the sides are (6, -1) and (4, 3).
    const auto first_side = [](int at_x, int at_y) { return at_x == 6 && at_y == -1; };
    assert((d2d::rules::leap_landing(0, 0, 5, 1, 10, as_is, first_side) == Spot{ { 6, -1 } }));
    const auto second_side = [](int at_x, int at_y) { return at_x == 4 && at_y == 3; };
    assert((d2d::rules::leap_landing(0, 0, 5, 1, 10, as_is, second_side) == Spot{ { 4, 3 } }));
    // Negative deltas: the C division truncates toward 0, (-300 / 35, -100 / 35) = (-8, -2).
    assert((d2d::rules::leap_landing(0, 0, -30, -10, 10, as_is, open) == Spot{ { -8, -2 } }));
    assert((d2d::rules::leap_landing(5, 5, -25, -5, 10, as_is, open) == Spot{ { -3, 3 } }));
    // Range 0: the point pulled back onto the caster.
    assert((d2d::rules::leap_landing(3, 3, 7, 6, 0, as_is, open) == Spot{ { 3, 3 } }));
    // A point at the caster: no side step, all three tries the caster's spot.
    assert((d2d::rules::leap_landing(5, 5, 5, 5, 10, as_is, open) == Spot{ { 5, 5 } }));
    assert(!d2d::rules::leap_landing(5, 5, 5, 5, 10, as_is, [](int, int) { return false; }));
    std::printf("leap landing: ok\n");
    return 0;
}
