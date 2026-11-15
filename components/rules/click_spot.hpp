// SPDX-License-Identifier: GPL-3.0-or-later
// Where a click on the ground walks to (FUN_004623c0, game.exe 1.14d):
// collision.md "What a held frame sends". The client works it out before it
// paths and sends C->S 0x01 / 0x03, in whole subtiles, from the unit's own
// subtile (its path's GetXPos / GetYPos).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>

namespace d2d::rules {

// FUN_00474080: the distance game.exe uses, an octagon's,
// (longer * 0x3d7 + shorter * 0x197) >> 10. It runs a little short (a step of
// 2 straight out is 1).
[[nodiscard]] constexpr int approx_distance(int delta_x, int delta_y) noexcept {
    const int across = delta_x < 0 ? -delta_x : delta_x, down = delta_y < 0 ? -delta_y : delta_y;
    return int(std::uint32_t(std::max(across, down) * 0x3d7 + std::min(across, down) * 0x197) >> 10);
}

// The click's flags, as FUN_00462d00 hands them to FUN_00462a20.
inline constexpr std::uint32_t kClickPress = 0x4, kClickHeld = 0x8, kClickRelease = 0x10, kClickRun = 0x40;

// The least distance a click's spot goes out to (LAB_004624a1..LAB_004624d6):
// a press 3, or 5 running; held 4, or 5 running; a release 0.
[[nodiscard]] constexpr int click_least_distance(std::uint32_t flags) noexcept {
    if ((flags & kClickPress) && (flags & kClickRun)) return 5;
    if (flags & kClickHeld) return 4 | int((flags & kClickRun) >> 6);
    if (flags & kClickRelease) return 0;
    return 3;
}

struct ClickSpot { int x = 0, y = 0; };

// The spot a click at (spot_x, spot_y) walks to from (unit_x, unit_y), or
// none: nothing goes when the spot is 0x100 or more away on an axis, or on
// the unit's own subtile (LAB_00462482). Nearer than click_least_distance
// (by approx_distance, taken as 1 under 2), the step is scaled out to it:
// the scale is a 32-bit float (FSTP at 0x4624e3) and each axis is truncated
// (FUN_00682fd0, CVTTSD2SI).
[[nodiscard]] inline std::optional<ClickSpot> click_spot(int unit_x, int unit_y, int spot_x, int spot_y, std::uint32_t flags) noexcept {
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

}  // namespace d2d::rules
