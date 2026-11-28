// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A standing burner (missile do 5: FUN_005ae520 -> FUN_005ae1f0 -> FUN_00641cb0) takes the first unit its
// footprint finds walking the missile's near rooms in list order, each room's units newest first. A unit in
// a later room is not preferred for being newer overall. Through the real Fight::missile_tick; made-up rows.

#include <ai/units.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <test_data.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::game;

// Check LAB_005a8850 -> FUN_005a8730's hostility gate before FUN_00641cb0's room ordering.
int main() try {
    d2d::test::TestData test;
    auto& fire_wall = test.skill(51, "Fire Wall");
    fire_wall.etype = 0;
    fire_wall.emin = fire_wall.emax = 40;
    fire_wall.hitshift = 8;
    test.monster(0, "zombie1");
    test.data.monsters.types[0].size = 3;                         // a 3x3 footprint: reaches the next subtile over
    test.mon_level(1, 100, 0, 0, 0);
    auto& flame = test.data.missiles["firewall"];
    flame.name = "firewall";
    flame.srv_do = 5;
    flame.size = 1;
    flame.skill = "Fire Wall";
    World world(&test.data, -1, -1);
    Level field;
    field.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    field.walk.assign(80 * 80, 0);
    field.rooms.push_back({ .x = 0, .y = 0, .width = 8, .height = 8 });   // subtiles 0..39
    field.rooms.push_back({ .x = 8, .y = 0, .width = 8, .height = 8 });   // 40..79, near room 0 (gap 0)
    world.level = &field;
    auto& fight = world.fight;
    fight.enter(&field);
    fight.skill_level = [](int) { return 1; };
    fight.skill_base = fight.skill_level;
    const auto centre = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
    const auto foe = [&](int id, int sub_x, int align = 0) {
        Monster monster;
        monster.type = 0;
        monster.id = id;
        monster.align = align;
        monster.unit.x = centre(sub_x); monster.unit.y = centre(20);
        monster.hit_points = monster.stats.hit_points = 100000;
        return monster;
    };
    // The flame stands in room 0 on subtile 39. Its room's newest monster is a converted ally, which
    // LAB_005a8850 -> FUN_005a8730 rejects; the older hostile monster in that room burns before the
    // hostile one in room 1, even though the latter is newer. All three are inside the footprint.
    fight.monsters.push_back(foe(100, 39));
    fight.monsters.push_back(foe(101, 39, 2));
    fight.monsters.push_back(foe(102, 40));
    std::uint32_t now = 1000 * 40;
    Missile missile{ &flame, centre(39), centre(20), 0, 0, 0, now, now + 100 * 40, {} };
    missile.friendly = true; missile.caster = world.actor->id; missile.skill = 51; missile.level = 1;
    fight.missiles.push_back(std::move(missile));
    now += 40;
    fight.missile_tick(now);
    assert(fight.monsters[0].hit_points < 100000);                // the first room's unit burns (FUN_00641cb0)
    assert(fight.monsters[1].hit_points == 100000);
    assert(fight.monsters[2].hit_points == 100000);
    std::printf("OK: a burner skips an allied monster, then takes the first hostile in near-room order\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
