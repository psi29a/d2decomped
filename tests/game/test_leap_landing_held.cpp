// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A leaping monster holds its landing while it flies (FUN_005d9d50 -> FUN_0064ea90: its pattern with
// bits 0x80 there, the unit bit 0x1000 on the centre subtile, cleared as it lands, FUN_005da120):
// another unit finds that subtile taken in the real Fight::crowd_around, and free once it has landed;
// a leap taken after the crowd was made holds it in that crowd too.
// Made-up rows (test_data.hpp).

#include <ai.hpp>
#include <cues.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <loot.hpp>
#include <rules.hpp>
#include <test_data.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::game;

int main() try {
    d2d::test::TestData fixture;
    fixture.monster(0, "sandleaper1");
    auto& data = fixture.data;
    Level arena;
    arena.id = 2;
    arena.ds1 = d2d::ds1::Map(30, 30, 0, 1);
    arena.walk.resize(150 * 150);
    const Level* level = &arena;
    d2d::rules::Rng random{ 7 };
    Cues cues;
    cues.game_data = &data;
    Loot loot{ &data, level, random, cues };
    loot.ground_level = level;
    Fight fight{ &data, level, random, loot, cues };
    Fight::Hero hero;
    fight.act_as(hero);
    Monster leaper;
    leaper.type = 0;
    leaper.hit_points = leaper.stats.hit_points = 10;
    leaper.unit.x = 10.1f; leaper.unit.y = 10.1f;
    leaper.unit.shape = UnitShape::monster(1, false, false, false);
    leaper.leaping = true;
    leaper.leap_to_x = 12.1f; leaper.leap_to_y = 10.1f;
    leaper.landing_mark.x = leaper.leap_to_x; leaper.landing_mark.y = leaper.leap_to_y;
    leaper.landing_mark.shape = UnitShape{ 1, 1, leaper.unit.shape.mask };
    fight.monsters.push_back(std::move(leaper));
    UnitState other;
    other.shape = UnitShape::monster(1, false, false, false);
    {
        Crowd crowd;
        fight.crowd_around(crowd, 11.f, 10.f);
        assert(crowd.at(12.1f, 10.1f, &other));                     // the landing, held while it flies
        assert(!crowd.at(12.5f, 10.1f, &other));                    // the next subtile is free: the centre alone
    }
    fight.monsters.back().leaping = false;                          // landed: the stamp's cleared
    {
        Crowd crowd;
        fight.crowd_around(crowd, 11.f, 10.f);
        assert(!crowd.at(12.1f, 10.1f, &other));
        // A leap taken after the frame's crowd was made (a monster earlier in Fight::world's loop):
        // its stamp is in the map at once, so a later monster's search finds it in the same crowd.
        auto& again = fight.monsters.back();
        again.landing_mark.x = 13.1f; again.landing_mark.y = 10.1f;
        again.leaping = true;
        assert(crowd.at(13.1f, 10.1f, &other));
    }
    std::puts("OK: a leap holds its landing till it lands, from the frame it takes off");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_leap_landing_held: %s\n", error.what());
    return 1;
}
