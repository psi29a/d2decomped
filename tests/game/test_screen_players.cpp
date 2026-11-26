// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// World::screen_players: a LAN joiner (own_screen, session/lan.cpp) plays on its own
// machine, so the host's screen isn't local co-op's. ClientSession::coop_partners reads
// it: two or more hide the HUD and fill the Party screen with the screen's players.
// A rule over the player list: made-up data (test_data.hpp), no MPQs.
#include "test_data.hpp"

#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <memory>

using namespace d2d::game;

int main() try {
    d2d::test::TestData fixture;
    World world(&fixture.data, -1, -1);
    assert(world.players.size() == 1 && world.screen_players() == 1);   // the host's own player

    // A LAN joiner: two players in the game, one on the host's screen.
    world.players.push_back(std::make_unique<World::Player>());
    world.players.back()->own_screen = true;
    std::printf("host + LAN joiner: %zu players, %d on the screen\n", world.players.size(), world.screen_players());
    assert(world.screen_players() == 1);

    // A pad partner (local co-op) shares the screen.
    world.players.push_back(std::make_unique<World::Player>());
    assert(world.players.size() == 3 && world.screen_players() == 2);

    std::printf("OK: a LAN joiner isn't on the host's screen\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_screen_players: %s\n", error.what());
    return 1;
}
