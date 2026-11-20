// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of cairn_effect.hpp's functions; their notes are in the header.
#include "cairn_effect.hpp"

#include "ambient_dip.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace d2d::rules {

std::optional<CairnClientEvent> cairn_client_event(bool has_player, bool has_room,
                                                   std::optional<std::pair<int, int>> stone,
                                                   int param1, int range) {
    if (!has_player || !has_room || !stone) return std::nullopt;
    const auto [subtile_x, subtile_y] = *stone;
    return CairnClientEvent{ 0x120, subtile_x, subtile_y, std::max(range, 1), cairn_dip(param1, range) };
}

}
