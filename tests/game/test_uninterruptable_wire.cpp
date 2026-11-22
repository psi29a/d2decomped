// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The player in state 0x36 (uninterruptable, FUN_005544b0: a Whirlwind, Leap
// or Leap Attack under way, Fight::uninterruptable) reaches the client's View
// across the view codec, so the inventory grid tints every item red
// (FUN_004c2240's second test, tint_cells).

#include <fight.hpp>
#include <gamedata.hpp>
#include <replication.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    GameData game_data;
    View sent;
    sent.level = &game_data.town;
    ViewEncoder encoder;
    View received;
    assert(apply_view(game_data, encode_view(game_data, sent, encoder), received) && !received.uninterruptable);
    sent.uninterruptable = true;
    assert(apply_view(game_data, encode_view(game_data, sent, encoder), received) && received.uninterruptable);
    // The World's own: a leap in flight puts it on.
    World world(&game_data, -1, -1);
    world.actor->leap = Fight::Leap{};
    assert(world.fight.uninterruptable() && world.view().uninterruptable);
    world.actor->leap.reset();
    assert(!world.view().uninterruptable);
    std::printf("OK: state 0x36 crosses the view codec\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
