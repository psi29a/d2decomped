// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's do 2 (FUN_005ae400 -> FUN_005a9720: viper_poisjav's trail): on each frame its flight enters a
// new subtile, its SubMissile1 standing there for Range + LevRange x level frames and its SubLoop's
// (Missile::trail_loops); a SrcDamage-0 sub with SrcMissDmg takes the missile's physical x SrcMissDmg / 128
// and none of its poison (FUN_0064b860, 0x64bd07). No game data: rows typed in.

#include <ai/units.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <missile_flight.hpp>
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

int main() try {
    Level open;
    open.ds1 = d2d::ds1::Map(9, 9, 1, 1);
    open.walk.assign(45 * 45, 0);
    open.rooms.resize(1);
    GameData::MissileInfo javelin, cloud;
    javelin.srv_do = 2; javelin.src_damage = 128; javelin.range = 15; javelin.subs[0] = &cloud; javelin.max_vel = 0;
    cloud.srv_do = 3; cloud.src_damage = 0; cloud.src_miss_dmg = 128; cloud.range = 60; cloud.sub_loop = 1; cloud.sub_start = 10; cloud.sub_stop = 19;
    Missile thrown;
    thrown.info = &javelin;
    thrown.x = 2.1f; thrown.y = 4.1f;
    thrown.subtile_x = 10; thrown.subtile_y = 20;
    thrown.velocity_x = 10.f; thrown.velocity_y = 0;
    thrown.aim_at = std::pair{ 8.1f, 4.1f };
    thrown.born = 0; thrown.dies = 15 * 40;
    thrown.src.a2_min = 10; thrown.src.a2_max = 20;
    thrown.src.poison_min = 300; thrown.src.poison_max = 400; thrown.src.poison_len = 100;
    thrown.trail_loops = 9 * 14;                                  // its SrvCalc1 over 0 at level 8: (19 - 10) x (8 x 2 - 2)
    thrown.owner = 5; thrown.owner_align = 0;
    std::vector<Missile> missiles;
    missiles.push_back(std::move(thrown));
    std::array<Foe, 1> foes{};
    foes[0].x = foes[0].y = 30.f;
    d2d::rules::Rng rng(7);
    for (std::uint32_t frame = 1; frame <= 12; ++frame)
        missiles_update(open, missiles, foes, rng, frame * 40, [](float, float) { return std::vector<std::array<int, 4>>{}; },
                        [](Missile&) { return false; }, [](int) { return true; }, [](Missile&) {});
    std::vector<std::pair<int, int>> spots;
    for (const auto& made : missiles) {
        if (made.info != &cloud) continue;
        assert(made.velocity_x == 0 && made.velocity_y == 0);
        assert(made.dies - made.born == std::uint32_t(60 + 9 * 14) * 40);
        assert(made.src.a2_min == 10 && made.src.a2_max == 20);              // the javelin's physical, x 128 / 128
        assert(made.src.poison_min == 0 && made.src.poison_max == 0);        // none of its poison
        assert(made.owner == 5);
        spots.emplace_back(int(std::floor(made.x * 5)), int(std::floor(made.y * 5)));
    }
    std::printf("viper trail: %zu clouds\n", spots.size());
    assert(spots.size() >= 3);
    for (std::size_t k = 1; k < spots.size(); ++k) assert(spots[k] != spots[k - 1]);   // one a subtile entered
    assert(std::ranges::none_of(spots, [](const auto& spot) { return spot == std::pair{ 10, 20 }; }));   // not its own start
    std::printf("test_viper_trail: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_viper_trail: %s\n", error.what());
    return 1;
}
