// SPDX-License-Identifier: GPL-3.0-or-later
// The other players as candidates of a click's Skills.txt SearchEnemyNear pick
// (FUN_00467490, ClientSession::near_pick), apart from the session so a test can run them.
#pragma once

#include <level.hpp>
#include <monsters.hpp>
#include <search_enemy.hpp>
#include <world.hpp>

#include <vector>

namespace d2d::client {

// Appends each of `view.others` to `units` (type 0; dead when a ghost or dead on the
// host; hit only when hostile) and its room entry to `listed`, in view order.
void add_near_players(const d2d::game::View& view, const d2d::game::Level* level,
                      std::vector<d2d::rules::NearUnit>& units, std::vector<d2d::rules::RoomUnit>& listed);

}  // namespace d2d::client
