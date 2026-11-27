// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Monsters follow the player over an outdoor edge (Fight::monsters_cross):
// game.exe's step (FUN_00554ca0) takes the path's next room if it's one of
// the act's rooms (FUN_005545c0), whatever its level. As the player walks
// over the edge (World::cross_level): the Blood Moor's monsters near the
// player come into the Cold Plains' list, moved by the offset, their leader,
// bound partner and skill target remapped; a gone copy keeps the Blood
// Moor's indices and isn't what an id finds; one far off, a corpse and a
// dead bound partner stay behind. After a town portal, the wild level left
// (still Fight's list) sends nobody. Made-up rows (test_data.hpp): the rule,
// not the game's numbers.

#include <ai/units.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <test_data.hpp>
#include <world.hpp>

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    d2d::test::TestData test;
    test.monster(0, "zombie1");
    World world(&test.data, -1, -1);
    // Two 60 x 60 levels side by side, the Cold Plains east of the Blood Moor (Level::nearby).
    Level moor{}, plains{};
    moor.id = d2d::rules::level_ids::kBloodMoor;
    plains.id = d2d::rules::level_ids::kColdPlains;
    moor.ds1 = d2d::ds1::Map(60, 60, 1, 1);
    plains.ds1 = d2d::ds1::Map(60, 60, 1, 1);
    moor.nearby.push_back({ &plains, 60, 0 });
    plains.nearby.push_back({ &moor, -60, 0 });
    world.level = &moor;
    auto& fight = world.fight;
    fight.enter(&moor);
    auto* actor = world.actor;
    const auto stand = [&](float x, float y) {
        actor->player.x = x; actor->player.y = y;
        actor->player.goal_x = actor->target_x = x; actor->player.goal_y = actor->target_y = y;
    };
    const auto make = [&](int id, float x, float y) {
        Monster monster;
        monster.type = 0;
        monster.id = id;
        monster.stats.hit_points = monster.hit_points = 100;
        monster.unit.x = x; monster.unit.y = y;
        monster.unit.goal_x = x; monster.unit.goal_y = y - 1;
        monster.home_x = x; monster.home_y = y;
        monster.area = 5;
        return monster;
    };
    stand(61, 30);                                           // just past the Blood Moor's east edge
    fight.monsters.push_back(make(1, 58, 31));               // 0: a leader, near
    fight.monsters.push_back(make(2, 59, 32));               // 1: its minion, near; its skill on its leader
    fight.monsters.back().leader = 0;
    fight.monsters.back().skill_unit = 0;
    fight.monsters.push_back(make(3, 5, 30));                // 2: far off, stays
    fight.monsters.push_back(make(4, 62, 29));               // 3: near, bound to 4
    fight.monsters.push_back(make(5, 10, 29));               // 4: far, its partner
    fight.monsters[3].bound = 4; fight.monsters[4].bound = 3;
    fight.monsters.push_back(make(6, 60, 30));               // 5: near but dead: its corpse stays
    fight.monsters.back().hit_points = 0;
    fight.monsters.push_back(make(8, 61, 31));               // 6: near, its skill on the corpse, bound to 7
    fight.monsters.back().skill_unit = 5;
    fight.monsters.push_back(make(9, 12, 31));               // 7: its dead partner, far: stays
    fight.monsters.back().hit_points = 0;
    fight.monsters[6].bound = 7; fight.monsters[7].bound = 6;
    fight.monsters[0].steps = { { 290, 155 } };
    fight.monsters[0].skill_x = 290; fight.monsters[0].skill_y = 150;
    fight.kept[&plains].push_back(make(7, 5, 30));           // the Cold Plains' own, already there

    world.cross_level();                                     // the plains are 60 east: dx 60
    assert(world.level == &plains && fight.mon_level == &plains && float(actor->player.x) == 1);
    assert(fight.monsters.size() == 6);
    const int ids[] = { 7, 1, 2, 4, 5, 8 };
    for (std::size_t index = 0; index < 6; ++index) assert(fight.monsters[index].id == ids[index]);
    const auto& leader = fight.monsters[1];
    assert(float(leader.unit.x) == -2 && float(leader.unit.y) == 31 && leader.unit.goal_x == -2 && leader.home_x == -2 && leader.home_y == 31);
    assert(leader.steps[0].first == -10 && leader.steps[0].second == 155 && leader.skill_x == -10 && leader.skill_y == 150);
    assert(leader.area == -1 && leader.leader == -1);
    assert(fight.monsters[2].leader == 1 && fight.monsters[2].skill_unit == 1);   // its leader's new place
    assert(fight.monsters[3].bound == 4 && fight.monsters[4].bound == 3);         // the bound pair crossed together
    assert(float(fight.monsters[4].unit.x) == -50);
    assert(fight.monsters[5].skill_unit == -1 && fight.monsters[5].bound == -1);  // the corpse and dead partner left behind

    const auto& left = fight.kept.at(&moor);
    assert(left.size() == 8);
    for (const std::size_t index : { 0, 1, 3, 4, 6 }) assert(left[index].gone && !left[index].alive());
    assert(!left[2].gone && left[2].alive() && float(left[2].unit.x) == 5);
    assert(!left[5].gone && left[5].hit_points == 0 && !left[7].gone);

    // Back over the edge: the same ones come back, the Cold Plains' own (near too) with them. An id
    // finds the one here, not the gone copy it left.
    stand(-1, 30);
    world.cross_level();
    assert(world.level == &moor && fight.mon_level == &moor && float(actor->player.x) == 59);
    assert(fight.monsters.size() == 8 + 6 && fight.monsters[8].id == 7 && float(fight.monsters[8].unit.x) == 65 && float(fight.monsters[9].unit.x) == 58);
    assert(fight.monster_index(1) == 9 && fight.monster_index(3) == 2 && fight.monster_index(6) == 5);

    // A town portal: World::level is the camp, Fight's list still the Blood Moor's (enter). Walking out
    // of the camp into another wild level then brings none of the Blood Moor's.
    Level camp{};
    camp.id = d2d::rules::level_ids::kRogueEncampment;
    world.level = &camp;
    fight.enter(&camp);
    assert(fight.mon_level == &moor);
    const auto plains_kept = fight.kept.at(&plains).size();  // the gone copies the walk back left
    fight.monsters_cross(&plains, 60, 0);
    assert(fight.kept.at(&plains).size() == plains_kept && fight.monsters.size() == 14 && !fight.monsters[9].gone);
    std::puts("OK: the monsters near the player follow it over the Blood Moor's edge and back; the far one, the corpse, the dead partner stay; none after a portal");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_monsters_cross: %s\n", error.what());
    return 1;
}
