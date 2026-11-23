// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The client's do 55 (FUN_004d8260, "ancient death center"): every third frame a cloud from a subtile
// -1..0 off it, sent -10..9 off, four steps of the missile's client seed (+0x20): its room's client
// stream stepped once, {low, 666} (FUN_00465fd0, client_unit_seed). No game data: rows typed in.

#include <ai/units.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <missile_flight.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <exception>
#include <utility>
#include <vector>

using namespace d2d::game;

int main() try {
    Level open;
    open.ds1 = d2d::ds1::Map(9, 9, 1, 1);
    open.walk.assign(40 * 40, 0);
    open.rooms.resize(1);
    open.rooms[0].x = open.rooms[0].y = 0;
    open.room1_seeds = { 77 };
    GameData::MissileInfo center, cloud;
    center.clt_do = 55;
    Missile ancient;
    ancient.info = &center;
    ancient.sub = &cloud;
    ancient.x = ancient.y = 4.1f;                  // subtile (20, 20), in the room
    ancient.born = 0; ancient.dies = 100000;
    std::vector<Missile> missiles;
    missiles.push_back(std::move(ancient));
    std::array<Foe, 1> foes{};
    foes[0].x = foes[0].y = 30.f;
    d2d::rules::Rng rng(7);
    const auto before = rng;
    missiles_update(open, missiles, foes, rng, 0, [](float, float) { return std::vector<std::array<int, 4>>{}; },
                    [](Missile&) { return false; }, [](int) { return true; }, [](Missile&) {});
    assert(missiles.size() == 2);
    d2d::rules::Rng room{ 77 };
    d2d::rules::Rng seed{ room.next() };            // the missile's: its room's stream stepped once
    const int off_x = int(seed.next() % 20) - 10, off_y = int(seed.next() % 20) - 10;
    const float from_x = 20.f + float(seed.next() & 1) - 1, from_y = 20.f + float(seed.next() & 1) - 1;
    const auto& made = missiles[1];
    assert(made.info == &cloud && made.visual_only);
    assert(std::abs(made.x - (from_x + 0.5f) / 5) < 1e-4f && std::abs(made.y - (from_y + 0.5f) / 5) < 1e-4f);
    assert(made.aim_at && std::abs(made.aim_at->first - (from_x + float(off_x) + 0.5f) / 5) < 1e-4f && std::abs(made.aim_at->second - (from_y + float(off_y) + 0.5f) / 5) < 1e-4f);
    assert(rng.low == before.low && rng.high == before.high);   // the update's rng untouched
    assert(open.client_rooms[0].low == d2d::rules::Rng{ 77 }.next());   // the room's stream stepped once
    std::printf("test_ancient_cloud: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_ancient_cloud: %s\n", error.what());
    return 1;
}
