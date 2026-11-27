// SPDX-License-Identifier: GPL-3.0-or-later
// near_players.hpp: the other players as Skills.txt SearchEnemyNear candidates (FUN_00467490).
#include "near_players.hpp"

#include <level.hpp>
#include <monsters.hpp>
#include <search_enemy.hpp>
#include <world.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace d2d::client {

// FUN_00467490's finder (mask 0x2003) takes players; FUN_00466870's hit test, for
// a live player outside town, is FUN_00465c60's player arm: FUN_004dc440(local,
// other, 8), our relation node toward it with the hostile bit (View::OtherPlayer
// hostile). FUN_00464f70 also admits a live player in the local player's party
// when an active hand has TargetAlly. A dead one (game.exe mode 0 / 0x11) goes
// as mode 0 and the finder rejects type 0 whether or not FUN_00465c60's
// dead-player corpse/loot arm passed. A local co-op ghost counts as dead.
void add_near_players(const d2d::game::View& view, const d2d::game::Level* level,
                      std::vector<d2d::rules::NearUnit>& units, std::vector<d2d::rules::RoomUnit>& listed,
                      std::span<const std::uint32_t> allied_players) {
    for (const auto& other : view.others) {
        const bool dead = other.ghost || other.dead;
        const bool allied = std::ranges::find(allied_players, other.id) != allied_players.end();
        units.push_back({ 0, other.unit.x.subtile(), other.unit.y.subtile(), dead ? 0 : 1, false,
                          near_player_hit(other.hostile, allied), false });
        listed.push_back({ level ? d2d::game::room_of(*level, other.unit.x, other.unit.y) : -1, true, other.unit.room_stamp });
    }
}

}  // namespace d2d::client
