// SPDX-License-Identifier: GPL-3.0-or-later
// The waypoint command's refusal window: game.exe 1.14d FUN_0054c5d0 / FUN_0055b720.
#pragma once

#include <cstdint>
#include <optional>

namespace d2d::rules {

// C->S 0x49 (FUN_0054c5d0): refused while GetTickCount is under the player's
// last hostile declaration (player data +0x160, written by FUN_0055b720 only,
// read by FUN_0055b6c0) plus 10000, a 32-bit sum that wraps; none declared:
// not refused here (game.exe's zeroed field is 0).
constexpr bool waypoint_refused(std::optional<std::uint32_t> hostile_at, std::uint32_t now_ms) {
    return hostile_at && now_ms < std::uint32_t(*hostile_at + 10000u);
}

}  // namespace d2d::rules
