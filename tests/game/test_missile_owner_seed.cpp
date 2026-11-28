// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster missile's to-hit rolls on its owner's unit seed (FUN_0057d9b0, ECX the owner) even when the owner isn't
// among the foes (Fight::world adds the monsters only while one is aligned or a CollideFriend missile flies):
// missiles_update's owner_seed finds it by id; the shared rng isn't stepped. No game data: rows typed in.

#include <ai/units.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <missile_flight.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

using namespace d2d::game;

int main() {
    Level open;
    open.ds1 = d2d::ds1::Map(9, 9, 1, 1);
    open.walk.assign(45 * 45, 0);
    open.rooms.resize(1);
    GameData::MissileInfo row;
    row.range = 20; row.to_hit = true;
    Missile shot;
    shot.info = &row;
    shot.x = shot.y = 4.1f;                                       // standing on the foe
    shot.born = 0; shot.dies = 2000;
    shot.src.a2_min = shot.src.a2_max = 5; shot.src.to_hit = 50; shot.src.level = 5;
    shot.owner = 7; shot.owner_align = 0;
    shot.seed = d2d::rules::Rng{ 99 };
    std::vector<Missile> missiles{ shot };
    std::array<Foe, 1> foes{};                                    // the player alone: the shooter isn't a foe
    foes[0].x = foes[0].y = 4.1f; foes[0].alive = true; foes[0].level = 1;
    d2d::rules::Rng rng(7), shooter(1234);
    const auto rng_before = rng, shooter_before = shooter;
    missiles_update(open, missiles, foes, rng, 40, [](float, float) { return std::vector<std::array<int, 4>>{}; },
                    [](Missile&) { return false; }, [](int) { return true; }, [](Missile&) {},
                    [&](int owner) { return owner == 7 ? &shooter : nullptr; });
    assert(shooter.low != shooter_before.low);                    // the to-hit on the shooter's seed
    assert(rng.low == rng_before.low && rng.high == rng_before.high);   // the shared rng untouched
    std::puts("test_missile_owner_seed: ok");
    return 0;
}
