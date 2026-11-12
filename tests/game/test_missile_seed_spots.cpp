// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Two missile picks that reseed the missile's own seed (+0x20) to {its subtile x, 666} and draw
// nothing from the update's: Baal Taunt's control (do 34, FUN_005b04a0, 0x5b0561) picks its
// submissile so each frame, the lightning control's end (hit 58, FUN_005acdf0, 0x5ace5f) its
// HitSubMissile1's subtile, x then y, and the poison ring (hit 59, FUN_005a9370) aims each
// spoke at its own offset.

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <rules.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <utility>
#include <vector>

using namespace d2d::game;

namespace {

void update(const Level& level, std::vector<Missile>& missiles, std::uint32_t now_ms) {
    std::array<Foe, 1> foes{};
    foes[0].x = foes[0].y = 30.f;
    d2d::rules::Rng rng(7);
    const auto before = rng;
    missiles_update(level, missiles, foes, rng, now_ms, [](float, float) { return std::vector<std::array<int, 4>>{}; },
                    [](Missile&) { return false; }, [](int) { return true; }, [](Missile&) {});
    assert(rng.low == before.low && rng.high == before.high);   // the update's rng isn't drawn
}

}  // namespace

int main() try {
    Level open;
    open.ds1 = d2d::ds1::Map(8, 8, 1, 1);           // 40 x 40 subtiles, nothing blocked
    open.walk.assign(40 * 40, 0);
    GameData::MissileInfo first, second, bolt;
    first.range = second.range = bolt.range = 10;
    bolt.vel = 10;

    // do 34: both submissiles every frame (delays 1), the pick rand(2) on {subtile x, 666}.
    GameData::MissileInfo control;
    control.srv_do = 34;
    control.range = 100;
    control.param2 = control.param3 = 1;
    control.subs = { &first, &second };
    for (const float at_x : { 4.1f, 5.3f, 6.5f, 7.7f }) {
        Missile taunt;
        taunt.info = &control;
        taunt.x = at_x; taunt.y = 4.f;
        taunt.born = 0; taunt.dies = 100000;
        std::vector<Missile> missiles{ std::move(taunt) };
        update(open, missiles, 400);
        assert(missiles.size() == 2);
        d2d::rules::Rng seed{ std::uint32_t(std::floor(at_x * 5)) };
        assert(missiles[1].info == (seed(2) == 0 ? &first : &second));
    }

    // hit 58: run out, HitSubMissile1 at a subtile within sHitPar1 (2), x then y on {subtile x, 666}.
    GameData::MissileInfo lightning;
    lightning.hit_func = 58;
    lightning.hit_par1 = 2;
    lightning.hit_sub_info = &bolt;
    Missile end;
    end.info = &lightning;
    end.x = 4.1f; end.y = 4.3f;                      // subtile (20, 21)
    end.born = 0; end.dies = 400;
    std::vector<Missile> missiles{ std::move(end) };
    update(open, missiles, 400);
    assert(missiles.size() == 1 && missiles[0].info == &bolt && missiles[0].to);
    d2d::rules::Rng seed{ 20 };
    const int to_x = 20 - 2 + seed(5), to_y = 21 - 2 + seed(5);
    assert(std::abs(missiles[0].to->first - (float(to_x) + 0.5f) / 5) < 0.001f);
    assert(std::abs(missiles[0].to->second - (float(to_y) + 0.5f) / 5) < 0.001f);

    // Both draws on its own subtile (sHitPar1 1: rand(3) twice 1, first at subtile x 6): FUN_0059fa30
    // (0x59fbbe) aims the moving sub missile, with no target unit, at (+1, +1).
    lightning.hit_par1 = 1;
    Missile own;
    own.info = &lightning;
    own.x = 6.5f / 5; own.y = 9.5f / 5;              // subtile (6, 9)
    own.born = 0; own.dies = 400;
    missiles = { std::move(own) };
    update(open, missiles, 400);
    assert(missiles.size() == 1 && missiles[0].to);
    assert(std::abs(missiles[0].to->first - 7.5f / 5) < 0.001f && std::abs(missiles[0].to->second - 10.5f / 5) < 0.001f);

    // hit 59: each spoke aims at its ring offset from the start (FUN_005a9370 flag 2), not (+1, +1).
    GameData::MissileInfo poison;
    poison.hit_func = 59;
    poison.hit_par1 = 16;                        // one odd spoke, way 1
    poison.hit_sub_info = &bolt;
    bolt.param1 = bolt.param2 = 1;
    Missile burst;
    burst.info = &poison;
    burst.x = 4.1f; burst.y = 4.3f;              // subtile (20, 21)
    burst.born = 0; burst.dies = 400;
    missiles = { std::move(burst) };
    update(open, missiles, 400);
    assert(missiles.size() == 9);
    const int at_x = 20, at_y = 21;
    for (int way = 0; way < 16; way += 2) {
        const float want_x = (float(at_x + kRingWayX[std::size_t(way)]) + 0.5f) / 5.f;
        const float want_y = (float(at_y + kRingWayY[std::size_t(way)]) + 0.5f) / 5.f;
        const auto spoke = std::ranges::find_if(missiles, [&](const Missile& laid) {
            return laid.to && std::abs(laid.to->first - want_x) < 0.001f && std::abs(laid.to->second - want_y) < 0.001f;
        });
        assert(spoke != missiles.end());
    }
    std::puts("OK: do 34 and hit 58 pick on {subtile x, 666}; a spot on its own subtile goes to (+1, +1)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_missile_seed_spots: %s\n", error.what());
    return 1;
}
