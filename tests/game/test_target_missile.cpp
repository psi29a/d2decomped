// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's missile-at-target (do 28: MonBlizzard, the meteors, the
// catapults) is made at the target, not at the caster (FUN_0056ede0 ->
// FUN_0059fa30 flag 1), heading a subtile on at (+1, +1). No game data.

#include <ai.hpp>
#include <gamedata.hpp>

#include <cassert>
#include <cmath>
#include <vector>

using namespace d2d::game;

int main() {
    GameData game_data;
    auto& row = game_data.missiles["centre"];
    row.name = "centre"; row.range = 25;
    Monster monster;
    monster.unit.x = 10; monster.unit.y = 20;
    std::vector<Missile> missiles;
    missile_at_target(game_data, monster, "centre", 3, -2, 0, missiles, 1);
    assert(missiles.size() == 1 && missiles[0].x == 13 && missiles[0].y == 18);   // every shipped row: Vel 0, it stands there
    assert(missiles[0].velocity_x == 0 && missiles[0].velocity_y == 0);
    row.vel = 10;
    missile_at_target(game_data, monster, "centre", 3, -2, 0, missiles, 1);
    assert(missiles[1].x == 13 && missiles[1].y == 18);                               // a moving one starts there too
    assert(missiles[1].velocity_x > 0 && std::abs(missiles[1].velocity_x - missiles[1].velocity_y) < 1e-4f);
    assert(missiles[1].to && missiles[1].to->first > 13 && missiles[1].to->second > 18);
    return 0;
}
