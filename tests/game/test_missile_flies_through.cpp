// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's missile that flies on through units (CollideKill 0) with ClientCol
// bursts its ExplosionMissile at each unit it meets (FUN_004d30c0 ->
// FUN_004d2d70 runs it before the CollideKill test) and flies on; one with no
// ClientCol tests no unit there. A guided one (do 7, FUN_005ae780) whose owner
// is gone is freed by the server (FUN_005adbb0 -> FUN_00555600, no hit, no
// remove packet for a missile: FUN_0053bda0), so no explosion goes off; its
// drawn copy flies on. No game data: rows typed in.

#include <ds1.hpp>
#include <gamedata.hpp>
#include <missile_flight.hpp>
#include <rules.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <utility>
#include <vector>

using namespace d2d::game;

namespace {

Level open_level() {
    Level open;
    open.ds1 = d2d::ds1::Map(8, 8, 1, 1);           // 40 x 40 subtiles, nothing blocked
    open.walk.assign(40 * 40, 0);
    return open;
}

// One monster missile flying east at subtile (20, 20) with a foe ahead; the frames run until
// it has crossed the foe. Returns the missiles left.
std::vector<Missile> fly_by(const GameData::MissileInfo& bolt, bool owner_lives, float foe_x = 4.6f) {
    const Level open = open_level();
    Missile flying;
    flying.info = &bolt;
    flying.x = flying.y = 4.1f;
    flying.velocity_x = 5.f;
    flying.aim_at = std::pair{ 7.9f, 4.1f };
    flying.born = 0; flying.dies = 100000;
    flying.src.no_hit_roll = true;
    std::vector<Missile> missiles;
    missiles.push_back(std::move(flying));
    std::array<Foe, 1> foes{};
    foes[0].x = foe_x; foes[0].y = 4.1f; foes[0].size = 1; foes[0].id = -2;
    d2d::rules::Rng rng(7);
    for (std::uint32_t now = 40; now <= 40 * 3; now += 40)
        missiles_update(open, missiles, foes, rng, now, [](float, float) { return std::vector<std::array<int, 4>>{}; },
                        [](Missile&) { return false; }, [owner_lives](int) { return owner_lives; }, [](Missile&) {});
    return missiles;
}

}  // namespace

int main() try {
    GameData::MissileInfo explosion;
    explosion.name = "fireexplode"; explosion.range = 12;
    GameData::MissileInfo bolt;
    bolt.size = 1; bolt.srv_do = 1; bolt.last_collide = true; bolt.collide_kill = false; bolt.vel = 24; bolt.explosion = &explosion;
    bolt.client_col = true;
    const auto count_explosions = [&](const std::vector<Missile>& missiles) {
        int count = 0;
        for (const auto& missile : missiles) count += missile.info == &explosion;
        return count;
    };
    const auto through = fly_by(bolt, true);
    assert(std::ranges::count_if(through, [&](const Missile& missile) { return missile.info == &bolt; }) == 1);   // flew on
    assert(count_explosions(through) == 1);                                    // one unit, one burst
    bolt.client_col = false;
    assert(count_explosions(fly_by(bolt, true)) == 0);                      // no ClientCol: the client tests no unit
    bolt.client_col = true; bolt.collide_kill = true;
    const auto spent = fly_by(bolt, true);
    assert(std::ranges::count_if(spent, [&](const Missile& missile) { return missile.info == &bolt; }) == 0 && count_explosions(spent) == 1);
    GameData::MissileInfo spirit;
    spirit.size = 1; spirit.srv_do = 7; spirit.vel = 24; spirit.collide_kill = true; spirit.explosion = &explosion;
    const auto freed = fly_by(spirit, false, 30.f);
    assert(count_explosions(freed) == 0);                                       // the server's free is no hit
    assert(freed.size() == 1 && freed[0].visual_only);                          // its drawn copy flies on
    const auto kept = fly_by(spirit, true, 30.f);
    assert(!kept.empty() && !kept[0].visual_only);
    std::puts("OK: a CollideKill 0 flier with ClientCol bursts at each unit; a guided one losing its owner is freed without a burst");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_missile_flies_through: %s\n", error.what());
    return 1;
}
