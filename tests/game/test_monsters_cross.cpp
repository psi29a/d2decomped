// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Monsters follow the player over an outdoor edge (Fight::monsters_cross):
// game.exe's step (FUN_00554ca0) takes the path's next room if it's one of
// the act's rooms (FUN_005545c0), whatever its level. With the game data, as
// the player walks over the edge (World::cross_level): the Blood Moor's
// monsters near the player come into the Cold Plains' list,
// moved by the offset, their leader and bound partner remapped; a gone copy
// keeps the Blood Moor's indices; one far off stays behind.

#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int cross_check(const GameData& data) {
    d2d::rules::Rng rng{ 7 };
    Character character;
    auto made = new_character(data, 0, "Crosser", false, true, rng);
    character.name = "Crosser";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(data, character.header, character.items, character.stats);
    auto world = std::make_unique<World>(&data, -1, -1);
    world->enter(character);
    // Two 60 x 60 levels side by side, the Cold Plains east of the Blood Moor (Level::nearby).
    Level moor{}, plains{};
    moor.id = d2d::rules::level_ids::kBloodMoor;
    plains.id = d2d::rules::level_ids::kColdPlains;
    moor.ds1 = d2d::ds1::Map(60, 60, 1, 1);
    plains.ds1 = d2d::ds1::Map(60, 60, 1, 1);
    moor.nearby.push_back({ &plains, 60, 0 });
    plains.nearby.push_back({ &moor, -60, 0 });
    world->level = &moor;
    auto& fight = world->fight;
    fight.enter(&moor);
    auto* actor = world->actor;
    const auto stand = [&](float x, float y) {
        actor->player.x = x; actor->player.y = y;
        actor->player.goal_x = actor->target_x = x; actor->player.goal_y = actor->target_y = y;
    };
    stand(61, 30);                                           // just past the Blood Moor's east edge
    const auto make = [&](int id, float x, float y) {
        Monster monster;
        monster.type = data.monsters.row("zombie1");
        monster.id = id;
        monster.stats.hit_points = monster.hit_points = 100;
        monster.unit.x = x; monster.unit.y = y;
        monster.unit.goal_x = x; monster.unit.goal_y = y - 1;
        monster.home_x = x; monster.home_y = y;
        monster.area = 5;
        return monster;
    };
    fight.monsters.push_back(make(1, 58, 31));               // 0: a leader, near
    fight.monsters.push_back(make(2, 59, 32));               // 1: its minion, near
    fight.monsters.back().leader = 0;
    fight.monsters.push_back(make(3, 5, 30));                // 2: far off, stays
    fight.monsters.push_back(make(4, 62, 29));               // 3: near, bound to 4
    fight.monsters.push_back(make(5, 10, 29));               // 4: far, its partner
    fight.monsters[3].bound = 4; fight.monsters[4].bound = 3;
    fight.monsters.push_back(make(6, 60, 30));               // 5: near but dead: its corpse stays
    fight.monsters.back().hit_points = 0;
    fight.monsters[0].steps = { { 290, 155 } };
    fight.monsters[0].skill_x = 290; fight.monsters[0].skill_y = 150;
    fight.kept[&plains].push_back(make(7, 5, 30));           // the Cold Plains' own, already there

    world->cross_level();                                    // the plains are 60 east: dx 60
    assert(world->level == &plains && fight.mon_level == &plains && float(actor->player.x) == 1);
    assert(fight.monsters.size() == 5);
    assert(fight.monsters[0].id == 7);
    const int ids[] = { 1, 2, 4, 5 };
    for (std::size_t index = 0; index < 4; ++index) assert(fight.monsters[index + 1].id == ids[index]);
    const auto& leader = fight.monsters[1];
    assert(float(leader.unit.x) == -2 && float(leader.unit.y) == 31 && leader.unit.goal_x == -2 && leader.home_x == -2 && leader.home_y == 31);
    assert(leader.steps[0].first == -10 && leader.steps[0].second == 155 && leader.skill_x == -10 && leader.skill_y == 150);
    assert(leader.area == -1 && leader.leader == -1);
    assert(fight.monsters[2].leader == 1);                    // its minion follows its leader's new place
    assert(fight.monsters[3].bound == 4 && fight.monsters[4].bound == 3);   // the bound pair crossed together
    assert(float(fight.monsters[4].unit.x) == -50);

    const auto& left = fight.kept.at(&moor);
    assert(left.size() == 6);
    for (const std::size_t index : { 0, 1, 3, 4 }) assert(left[index].gone && !left[index].alive());
    assert(!left[2].gone && left[2].alive() && float(left[2].unit.x) == 5);
    assert(!left[5].gone && left[5].hit_points == 0);

    // Back over the edge: the same ones come back, the Cold Plains' own (near too) with them.
    stand(-1, 30);
    world->cross_level();
    assert(world->level == &moor && fight.mon_level == &moor && float(actor->player.x) == 59);
    assert(fight.monsters.size() == 6 + 5 && fight.monsters[6].id == 7 && float(fight.monsters[6].unit.x) == 65 && float(fight.monsters[7].unit.x) == 58);
    std::puts("OK: the monsters near the player follow it over the Blood Moor's edge and back; the far one and the corpse stay");
    return 0;
}

}  // namespace

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    return cross_check(*data);
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_monsters_cross: %s\n", error.what());
    return 1;
}
