// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of stone_scroll.hpp's functions; their notes are in the header.
#include "stone_scroll.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace d2d::rules {

std::optional<StoneSymbol> stone_symbol(const std::array<int, 5>& order, std::size_t place, std::uint32_t steps) {
    if (steps <= kStoneSymbolDelay[place]) return std::nullopt;
    const std::uint32_t age = steps - kStoneSymbolDelay[place];
    const int stone = order[place];
    if (stone < 0 || stone > 4) return std::nullopt;
    const int mode = age < 5 ? 0 : age < 10 ? 1 : age < 15 ? 2 : 5;
    const auto spot = kStoneSymbolSpot[std::size_t(stone)], glow = kStoneGlowSpot[std::size_t(stone)];
    return StoneSymbol{ int(place), stone, spot[0], spot[1], mode, glow[0], glow[1], age > 20 ? 0 : int(age), age == 1 };
}

}
