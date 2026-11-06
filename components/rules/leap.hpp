// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "monsters.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace d2d::rules {

// Leap's landing (FUN_005d9a10, 0x5d9a10), in subtiles: the point pulled
// back to `range` (aurarangecalc) by the AI distance (FUN_006417f0, at
// least 1; C division each axis), then three tries: the point, and a step
// either side across the way (sx = y - cy, sy = cx - x, each held to -2..2
// past -1 / 1): the nearest free spot to it (`free_spot`: FUN_0064e7b0,
// the caster's size, mask 0x1c09), a clear line to it (`line_clear`:
// FUN_00645910 -> FUN_0064e260, mask 0x804, no sizes) and within range.
// The first that passes; none, no leap.
template <class FreeSpot, class LineClear>
std::optional<std::pair<int, int>> leap_landing(int caster_x, int caster_y, int point_x, int point_y, int range, FreeSpot&& free_spot, LineClear&& line_clear) {
    const int span = std::max(ai_distance(point_x - caster_x, point_y - caster_y), 1);
    if (range < span) {
        point_x = (point_x - caster_x) * range / span + caster_x;
        point_y = (point_y - caster_y) * range / span + caster_y;
    }
    const auto held = [](int step) { return step < -1 ? -2 : step > 1 ? 2 : step; };
    const int side_x = held(point_y - caster_y), side_y = held(caster_x - point_x);
    const std::array<std::pair<int, int>, 3> tries{ { { point_x, point_y }, { point_x + side_x, point_y + side_y }, { point_x - side_x, point_y - side_y } } };
    for (const auto& [try_x, try_y] : tries) {
        const std::optional<std::pair<int, int>> spot = free_spot(try_x, try_y);
        if (!spot) continue;
        if (line_clear(spot->first, spot->second) && ai_distance(spot->first - caster_x, spot->second - caster_y) <= range) return spot;
    }
    return std::nullopt;
}

}  // namespace d2d::rules
