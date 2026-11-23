// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Local co-op's leash (docs/design/local-coop.md; not a game.exe rule): a
// move that spreads the party past kLeashWidth x kLeashHeight screen pixels
// is undone, the fight's too: a leap in flight or a knockback's slide that
// carries a player off is cut off where they stood. No game data.
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <world.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <exception>
#include <utility>
#include <vector>

using namespace d2d::game;

int main() try {
    GameData data;
    Level field;
    field.ds1 = d2d::ds1::Map(40, 40, 1, 1);
    field.walk.assign(std::size_t(40 * 5) * std::size_t(40 * 5), 0);
    World world(&data, -1, -1);
    world.level = &field;
    auto& lead = *world.players.front();
    auto& partner = world.add_player();
    lead.player.x = lead.player.y = 10;
    partner.player.x = partner.player.y = 12;
    const std::vector<std::pair<float, float>> before{ { 10.f, 10.f }, { 12.f, 12.f } };

    // A leap carries the partner 20 cells across the screen (x - y: 1600 px).
    partner.leap = Fight::Leap{ 12, 12, 32, 12, 0, 1000, 0, 1 };
    partner.player.x = 32;
    world.leash(before);
    assert(partner.player.x == 12 && partner.player.y == 12 && !partner.leap);

    // A knockback's slide the same way.
    partner.knock = std::array{ 12.f, 12.f, 31.f, 12.f };
    partner.player.x = 31;
    world.leash(before);
    assert(partner.player.x == 12 && !partner.knock);

    // Within the screen: left alone.
    partner.leap = Fight::Leap{ 12, 12, 13, 12, 0, 1000, 0, 1 };
    partner.player.x = 13;
    world.leash(before);
    assert(partner.player.x == 13 && partner.leap);
    std::puts("OK: the leash undoes a leap's or a knockback's spread");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_leash_fight_moves: %s\n", error.what());
    return 1;
}
