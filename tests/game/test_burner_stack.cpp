// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The player's burners (missile do 5: Fire Wall's flames), through the real Fight::missile_tick. Each flame
// is its own collide (FUN_005ae1f0 -> FUN_00641cb0, then FUN_005adf10): the first unit its footprint finds,
// that one alone; no list spans missiles (FUN_0064a470 is the missile's own last unit). So two flames on
// one monster burn it twice a frame, and one flame on two monsters burns one: the newest (a room's list head).
// Made-up rows (test_data.hpp): the rule, not the game's numbers.

#include <ai.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <test_data.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::game;

int main() try {
    d2d::test::TestData test;
    auto& fire_wall = test.skill(51, "Fire Wall");
    fire_wall.etype = 0;                                          // fire, 256 x 40 a frame: whole points each burn
    fire_wall.emin = fire_wall.emax = 40;
    fire_wall.hitshift = 8;
    test.monster(0, "zombie1");
    test.mon_level(1, 100, 0, 0, 0);
    auto& flame = test.data.missiles["firewall"];
    flame.name = "firewall";
    flame.srv_do = 5;                                             // FUN_005ae520: burns who stands in it
    flame.size = 1;
    flame.skill = "Fire Wall";
    World world(&test.data, -1, -1);
    Level open;
    open.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    open.walk.assign(80 * 80, 0);
    world.level = &open;
    auto& fight = world.fight;
    fight.enter(&open);
    fight.skill_level = [](int) { return 1; };
    fight.skill_base = fight.skill_level;
    constexpr float kX = 8.1f, kY = 8.1f;
    std::uint32_t now = 1000 * 40;
    const auto put = [&](int count) {
        fight.monsters.clear();
        for (int k = 0; k < count; ++k) {
            Monster monster;
            monster.type = 0;
            monster.id = 100 + k;
            monster.unit.x = kX; monster.unit.y = kY;
            monster.hit_points = monster.stats.hit_points = 100000;
            fight.monsters.push_back(std::move(monster));
        }
    };
    const auto flames = [&](int count) {
        fight.missiles.clear();
        for (int k = 0; k < count; ++k) {
            Missile missile{ &flame, kX, kY, 0, 0, 0, now, now + 100 * 40, {} };
            missile.friendly = true; missile.caster = world.actor->id; missile.skill = 51; missile.level = 1;
            fight.missiles.push_back(std::move(missile));
        }
        now += 40;
        fight.missile_tick(now);
    };
    const auto lost = [&](std::size_t index) { return 100000 - fight.monsters[index].hit_points; };
    put(1);
    flames(1);
    const int one = lost(0);
    put(1);
    flames(2);
    const int two = lost(0);
    assert(one > 0 && two == 2 * one);                              // the second flame burns too
    put(2);
    flames(1);
    assert(lost(1) == one && lost(0) == 0);                         // one flame, one unit: the newest
    std::printf("OK: one flame %d, two flames %d; a flame burns one unit\n", one, two);
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
