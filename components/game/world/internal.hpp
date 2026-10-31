// SPDX-License-Identifier: GPL-3.0-or-later
// What the world/ files share: no part of the game's API.
#pragma once

#include "ai.hpp"
#include "gamedata.hpp"

#include <vector>

namespace d2d::game {

// `level`'s units states, the ones its rooms made since (populate) added as they start.
void grow_states(std::vector<UnitState>& states, const Level& level);

}  // namespace d2d::game
