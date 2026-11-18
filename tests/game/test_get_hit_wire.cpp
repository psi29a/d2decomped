// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// What the player's get-hit draw reads (town/draw.cpp: FUN_00463390's
// frame at the get-hit rate, ending a tick early) crosses the view codec:
// its FHR, when the mode ends and whether a knockback holds it. Every View
// goes through the codec, a local game's too (Town::publish).

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
    sent.pmode = kModeGH;
    sent.pmode_until = 4520; sent.knocked = true; sent.fhr = 48;
    ViewEncoder encoder;
    View received;
    assert(apply_view(game_data, encode_view(game_data, sent, encoder), received));
    assert(received.pmode == kModeGH &&received.pmode_until == 4520 && received.knocked && received.fhr == 48);

    // The next hit, no knockback, new gear: a delta carries each change.
    sent.pmode_until = 6040; sent.knocked = false; sent.fhr = 13;
    assert(apply_view(game_data, encode_view(game_data, sent, encoder), received));
    assert(received.pmode_until == 6040 && !received.knocked && received.fhr == 13);
    std::printf("OK: the player's get-hit timing round-trips the view codec\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
