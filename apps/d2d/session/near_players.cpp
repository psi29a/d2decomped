// SPDX-License-Identifier: GPL-3.0-or-later
// near_players.hpp: the other players as Skills.txt SearchEnemyNear candidates (FUN_00467490).
#include "near_players.hpp"

#include <level.hpp>
#include <monsters.hpp>
#include <search_enemy.hpp>
#include <world.hpp>

#include <vector>

namespace d2d::client {

// FUN_00467490's finder (mask 0x2003) takes players; FUN_00466870's hit test, for
// a live player outside town, is FUN_00465c60's player arm: FUN_004dc440(local,
// other, 8), our relation node toward it with the hostile bit (View::OtherPlayer
// hostile). A dead one (game.exe's mode 0 / 0x11) goes as mode 0: rules::near_dead,
// left out unless the skill takes corpses. A local co-op ghost counts as dead.
// ponytail: FUN_00465c60's dead-player arm (its corpse ours, or loot bit 1 through
// FUN_0047a4f0) and FUN_00466870's FUN_00464ec0 / FUN_00464f70 passes aren't ported.
void add_near_players(const d2d::game::View& view, const d2d::game::Level* level,
                      std::vector<d2d::rules::NearUnit>& units, std::vector<d2d::rules::RoomUnit>& listed) {
    for (const auto& other : view.others) {
        const bool dead = other.ghost || other.dead;
        units.push_back({ 0, other.unit.x.subtile(), other.unit.y.subtile(), dead ? 0 : 1, false, other.hostile, false });
        listed.push_back({ level ? d2d::game::room_of(*level, other.unit.x, other.unit.y) : -1, true, other.unit.room_stamp });
    }
}

}  // namespace d2d::client
