// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Radament's death missile (radamentdeath, 400 frames) and its client do 32 (FUN_004d6c00): each frame,
// its frames left against the table at 0x6db848 ({300, 25}, {250, 10}, {100, 8}, {75, 6}, {30, 4}, then
// {0, 100} past the end); a radamenthandofgod where it stands on a frame left the period divides.
// Over its life: 375, 350, 325; 300 .. 260 by 10; 248 .. 104 by 8; 96 .. 78 by 6; 72 .. 32 by 4: 42.

#include <ai/units.hpp>
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
#include <span>
#include <utility>
#include <vector>

using namespace d2d::game;

int main() try {
    GameData::MissileInfo death;
    death.name = "radamentdeath"; death.clt_do = 32; death.range = 400;
    GameData::MissileInfo hand;
    hand.name = "radamenthandofgod"; hand.range = 21;
    Level open;
    open.ds1 = d2d::ds1::Map(8, 8, 1, 1);
    open.walk.assign(40 * 40, 0);
    std::vector<Missile> missiles;
    Missile made{ &death, 4.f, 4.f, 0, 0, 0, 0, 400 * 40, {} };
    made.visual_only = true;
    made.sub = &hand;
    missiles.push_back(std::move(made));
    d2d::rules::Rng rng{ 7 };
    std::array<Foe, 1> nobody{};
    int hands = 0;
    std::vector<int> lefts;
    for (std::uint32_t now = 40; now <= 400 * 40 + 40; now += 40) {
        const auto before = std::ranges::count_if(missiles, [&](const Missile& missile) { return missile.info == &hand && missile.born == now; });
        missiles_update(open, missiles, std::span<Foe>(nobody.data(), 0), rng, now, [](float, float) { return std::vector<std::array<int, 4>>{}; },
                        [](Missile&) { return false; }, [](int) { return true; }, [](Missile&) {});
        const auto made_now = std::ranges::count_if(missiles, [&](const Missile& missile) { return missile.info == &hand && missile.born == now; });
        if (made_now > before) { hands += int(made_now - before); lefts.push_back(400 - int(now / 40)); }
    }
    assert(hands == 42);
    assert(lefts.front() == 375 && lefts[3] == 300 && lefts.back() == 32);
    assert(std::ranges::none_of(lefts, [](int left) { return left <= 30; }));   // {0, 100}: none in the last 30 frames
    std::printf("OK: %d Hands of God over Radament's death\n", hands);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_radament_hands: %s\n", error.what());
    return 1;
}
