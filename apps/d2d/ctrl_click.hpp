// SPDX-License-Identifier: GPL-3.0-or-later
// Ctrl + left click on an item in a grid, the hand empty: FUN_0048ffe0
// tests Ctrl (GetAsyncKeyState(VK_CONTROL) at 0x4901f4) before Shift's
// belt; FUN_004912a0 calls it from three sites (0x49144d, 0x4916e7,
// 0x491750) and the branch reads no page, so d2d applies it on each grid.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace d2d::client::ctrl_click {

enum class Outcome { none, sell, refused };

// - a store open (FUN_004b3230, its NPC FUN_00463990): an item a vendor
//   buys (FUN_0062a130: rules::sellable) is sold, C->S 0x33 at
//   FUN_0062fdc0's price (0x490261); one it doesn't plays the hero's
//   "impossible" (FUN_004cb9c0 message 0x13, 0x490283) and sends nothing;
// - no store: nothing (0x4902f8): the click is taken, the item stays put.
[[nodiscard]] inline Outcome outcome(bool store, bool sellable) {
    if (!store) return Outcome::none;
    return sellable ? Outcome::sell : Outcome::refused;
}

// Message 0x13's line by d2s class (FUN_004cb9c0: the class table's +8).
[[nodiscard]] inline std::string_view impossible_voice(int cls) {
    static constexpr std::array<std::string_view, 7> kImpossible{ "amazon_impossible_1", "sorceress_impossible_1", "necromancer_impossible_1",
        "paladin_impossible_1", "barbarian_impossible_1", "druid_impossible_1", "assassin_impossible_1" };
    return kImpossible[std::size_t(std::clamp(cls, 0, 6))];
}

}  // namespace d2d::client::ctrl_click
