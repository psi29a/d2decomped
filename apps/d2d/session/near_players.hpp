// SPDX-License-Identifier: GPL-3.0-or-later
// The other players as candidates of a click's Skills.txt SearchEnemyNear pick
// (FUN_00467490, ClientSession::near_pick), apart from the session so a test can run them.
#pragma once

#include <search_enemy.hpp>
#include <search_walk.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace d2d::game {
struct Level;
struct View;
}

namespace d2d::client {

// Appends each of `view.others` to `units` (type 0; dead when a ghost or dead on the
// host; hit when hostile or named in `allied_players`) and its room entry to
// `listed`, in view order.
void add_near_players(const d2d::game::View& view, const d2d::game::Level* level,
                      std::vector<d2d::rules::NearUnit>& units, std::vector<d2d::rules::RoomUnit>& listed,
                      std::span<const std::uint32_t> allied_players = {});

// FUN_00464f70's TargetAlly pass for a player, after FUN_00465c60's hostile gate.
[[nodiscard]] constexpr bool near_player_hit(bool hostile, bool allied) { return hostile || allied; }

// FUN_00464ec0 / FUN_00464f70's reachable owner and party exceptions for a
// monster target under the active hand skill.
[[nodiscard]] constexpr bool skill_cursor_exception(bool target_pet, bool target_ally,
                                                     std::uint32_t owner, std::uint32_t self,
                                                     bool owner_same_party) {
    return (target_pet && owner != 0 && owner == self) || (target_ally && owner_same_party);
}

}  // namespace d2d::client
