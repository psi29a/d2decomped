// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Mephisto's death control (mephistodeathcontrol, 275 frames) and its client do 26 (FUN_004d4f20):
// a fire wall maker every CltParam1 (16) frames and a flying rock every CltParam2 (4), each aimed
// rand(2n + 1) - n subtiles off on its client seed (n CltParam3, 20); the makers (client do 27,
// FUN_004d5090) lay their fire on each new subtile and turn by 45 degrees x 3/4 on seed steps.
// Over its life: makers on frames 1, 17 .. 273 (18), rocks on 1, 5 .. 273 (69); a maker's turns bend
// its flight (FUN_00649970). Made-up rows.

#include <ai.hpp>
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
#include <span>
#include <utility>
#include <vector>

using namespace d2d::game;

int main() try {
    GameData::MissileInfo fire;
    fire.name = "mephistofirewall"; fire.range = 25;
    GameData::MissileInfo maker;
    maker.name = "mephistofirewallmaker"; maker.clt_do = 27; maker.range = 50; maker.vel = 8; maker.max_vel = 8; maker.clt_params = { 4, 0, 0, 0, 0 }; maker.clt_sub_info = &fire;
    GameData::MissileInfo rock;
    rock.name = "mephistoflyingrocksbig"; rock.range = 10; rock.vel = 12;
    GameData::MissileInfo control;
    control.name = "mephistodeathcontrol"; control.clt_do = 26; control.range = 275; control.clt_params = { 16, 4, 20, 0, 0 };
    control.clt_sub_info = &maker; control.clt_sub2_info = &rock;
    Level open;
    open.ds1 = d2d::ds1::Map(40, 40, 1, 1);
    open.walk.assign(200 * 200, 0);
    std::vector<Missile> missiles;
    Missile made{ &control, 20.f, 20.f, 0, 0, 0, 0, 275 * 40, {} };
    made.visual_only = true; made.level = 1;
    missiles.push_back(made);
    d2d::rules::Rng rng{ 7 };
    std::array<Foe, 1> nobody{};
    int makers = 0, rocks = 0, fires = 0;
    for (std::uint32_t now = 40; now <= 275 * 40 + 80; now += 40) {
        const auto born_now = [&](const GameData::MissileInfo& info) {
            return int(std::ranges::count_if(missiles, [&](const Missile& missile) { return missile.info == &info && missile.born == now; }));
        };
        missiles_update(open, missiles, std::span<Foe>(nobody.data(), 0), rng, now, [](float, float) { return std::vector<std::array<int, 4>>{}; },
                        [](Missile&) { return false; }, [](int) { return true; }, [](Missile&) {});
        makers += born_now(maker); rocks += born_now(rock); fires += born_now(fire);
    }
    std::printf("makers %d, rocks %d, fires %d\n", makers, rocks, fires);
    assert(makers == 18 && rocks == 69 && fires > makers);
    std::puts("OK: Mephisto's death control lays its makers, rocks and fires");

    // A maker alone: its turns re-aim the flight itself (`retarget`), so where it goes bends off its first way.
    missiles.clear();
    Missile alone{ &maker, 20.f, 20.f, missile_speed(maker, 1), 0, 0, 0, 50 * 40, {} };   // east
    alone.visual_only = true; alone.level = 1;
    alone.aim_at = std::pair{ 24.f, 20.f };
    alone.way_x = 20; alone.subtile_x = 100; alone.subtile_y = 100;
    missiles.push_back(alone);
    float turned = 0;
    for (std::uint32_t now = 40; now <= 48 * 40; now += 40) {
        missiles_update(open, missiles, std::span<Foe>(nobody.data(), 0), rng, now, [](float, float) { return std::vector<std::array<int, 4>>{}; },
                        [](Missile&) { return false; }, [](int) { return true; }, [](Missile&) {});
        const auto found = std::ranges::find(missiles, &maker, &Missile::info);
        if (found != missiles.end()) turned = std::max(turned, std::abs(found->y - 20.f));
    }
    std::printf("maker strayed %.2f cells off its first line\n", double(turned));
    assert(turned > 0.5f);
    std::puts("OK: a maker's turns bend its flight");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_mephisto_death: %s\n", error.what());
    return 1;
}
