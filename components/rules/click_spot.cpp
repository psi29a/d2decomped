// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of click_spot.hpp's functions; their notes are in the header.
#include "click_spot.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>

namespace d2d::rules {

std::optional<ClickSpot> click_spot(int unit_x, int unit_y, int spot_x, int spot_y, std::uint32_t flags) noexcept {
    int delta_x = spot_x - unit_x, delta_y = spot_y - unit_y;
    if (std::abs(delta_x) >= 0x100 || std::abs(delta_y) >= 0x100) return std::nullopt;
    if (delta_x == 0 && delta_y == 0) return std::nullopt;
    const int distance = std::max(approx_distance(delta_x, delta_y), 1);
    if (const int least = click_least_distance(flags); distance < least) {
        const float scale = float(double(least) / double(distance));
        delta_x = int(double(delta_x) * double(scale));
        delta_y = int(double(delta_y) * double(scale));
    }
    return ClickSpot{ unit_x + delta_x, unit_y + delta_y };
}

}
