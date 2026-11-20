// SPDX-License-Identifier: GPL-3.0-or-later
// The Cairn Stones' client event (S->C 0x89 kind 1, entry LAB_0046aee0).
#pragma once

#include "ambient_dip.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace d2d::rules {

struct CairnClientEvent {
    int missile = 0x120;
    int subtile_x = 0, subtile_y = 0;
    int frames = 0;
    DipFrames dip{};
};

// LAB_0046aee0: no local player, no room or no StoneAlpha (object 17) in
// that room's near list does nothing.
std::optional<CairnClientEvent> cairn_client_event(bool has_player, bool has_room,
                                                   std::optional<std::pair<int, int>> stone,
                                                   int param1, int range);

}  // namespace d2d::rules
