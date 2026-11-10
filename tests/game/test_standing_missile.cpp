// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's standing missile (no velocity) strikes from its own subtile
// by size (FUN_005ae1f0 -> FUN_00641cb0, rules::missile_touches): a Size 1
// one takes a size 1 foe on its own subtile only, so not one a subtile off,
// though that's within 0.4 cells; a size 2 foe in the plus. A flight that
// ends at a wall calls `flight_over` (FUN_00554ca0 gives 2: FUN_005adf10(0,
// 1), the hit with no unit); one in the open doesn't. No game data: a row
// typed in.

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <utility>
#include <vector>

using namespace d2d::game;

int main() try {
    GameData::MissileInfo shard;
    shard.size = 1;
    Level open;
    open.ds1 = d2d::ds1::Map(8, 8, 1, 1);           // 40 x 40 subtiles, nothing blocked
    open.walk.assign(40 * 40, 0);
    const auto strikes = [&](float foe_x, int size) {
        Missile standing;
        standing.info = &shard;
        standing.x = standing.y = 4.1f;             // subtile (20, 20)
        standing.born = 0; standing.dies = 100000;
        std::vector<Missile> missiles;
        missiles.push_back(std::move(standing));
        std::array<Foe, 1> foes{};
        foes[0].x = foe_x; foes[0].y = 4.1f; foes[0].size = size;
        d2d::rules::Rng rng(7);
        missiles_update(open, missiles, foes, rng, 4000, [](float, float) { return std::vector<std::array<int, 4>>{}; },
                        [](Missile&) { return false; }, [](int) { return true; }, [](Missile&) {});
        return missiles.empty();                    // spent on the foe it struck
    };
    assert(strikes(4.15f, 1));                      // its own subtile
    assert(!strikes(4.3f, 1));                      // one east (0.2 cells): not
    assert(strikes(4.3f, 2) && !strikes(4.5f, 2));  // size 2: the plus
    GameData::MissileInfo bolt;
    const auto flies_to_wall = [&](bool walled) {
        Level room = open;
        if (walled)
            for (int row = 0; row < 40; ++row) room.walk[std::size_t(row * 40 + 25)] = 0x04;   // a wall down subtile column 25
        Missile flying;
        flying.info = &bolt;
        flying.friendly = true;
        flying.x = flying.y = 4.1f;                 // subtile (20, 20), sent east at subtile 39
        flying.velocity_x = 5.f;
        flying.to = std::pair{ 7.9f, 4.1f };
        flying.born = 0; flying.dies = 100000;
        std::vector<Missile> missiles;
        missiles.push_back(std::move(flying));
        std::array<Foe, 1> foes{};
        foes[0].x = foes[0].y = 30.f;
        d2d::rules::Rng rng(7);
        int over = 0;
        for (std::uint32_t now = 40; now <= 40 * 10 && !missiles.empty(); now += 40)   // short of subtile 39 in the open
            missiles_update(room, missiles, foes, rng, now, [](float, float) { return std::vector<std::array<int, 4>>{}; },
                            [](Missile&) { return false; }, [](int) { return true; }, [&](Missile&) { ++over; });
        return over;
    };
    assert(flies_to_wall(true) == 1 && flies_to_wall(false) == 0);
    std::puts("OK: a standing monster missile strikes by its size, not within 0.4 cells; a flight over at a wall goes off");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_standing_missile: %s\n", error.what());
    return 1;
}
