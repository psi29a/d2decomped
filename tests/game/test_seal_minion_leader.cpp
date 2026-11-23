// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A seal's superunique's minions (FUN_00545c30 -> FUN_0054e600) follow it:
// each joins its group (FUN_0058f380), so the minion's leader is the boss
// (Monster::leader, its index in the level's list), as a placed group's.
// Made-up rows (test_data.hpp): the rule, not the game's numbers.

#include <character.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <test_data.hpp>
#include <uniques.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    d2d::test::TestData fixture;
    auto& data = fixture.data;
    fixture.monster(0, "fingermage1").minion[0] = 1;              // the boss's type, its minions' the next row
    fixture.monster(1, "doomknight1");
    fixture.mon_level(1, 100, 10, 10, 10);
    data.superuniques.resize(0x27);
    data.superuniques[0x26] = { .name = "Grand Vizier of Chaos", .type = 0, .min_grp = 3, .max_grp = 3 };
    Character character;
    character.name = "Headless";
    World world(&data, -1, -1);
    world.enter(character);
    Level open;
    open.id = 108;                                                // the Chaos Sanctuary
    open.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    open.walk.assign(80 * 80, 0);
    world.level = &open;
    world.fight.enter(&open);
    world.fight.monsters.clear();
    world.seal_boss(open, 0x26, 40, 40);
    const auto& monsters = world.fight.monsters;
    assert(monsters.size() == 4 && monsters[0].boss == d2d::rules::Boss::superunique && monsters[0].type == 0);
    for (std::size_t k = 1; k < monsters.size(); ++k) assert(monsters[k].boss == d2d::rules::Boss::minion && monsters[k].type == 1 && monsters[k].leader == 0);
    std::printf("test_seal_minion_leader: %zu minions follow their boss\n", monsters.size() - 1);
    return 0;
} catch (const std::exception& error) {
    std::printf("test_seal_minion_leader: %s\n", error.what());
    return 1;
}
