// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The merc's faster hit recovery crosses the view codec, so a joined game
// draws its get-hit with the host's FHR (town/view.cpp), not 0.

#include <gamedata.hpp>
#include <replication.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    GameData game_data;
    game_data.mercs[1];
    View sent;
    sent.level = &game_data.town;
    sent.merc = View::Merc{ {}, &game_data.mercs[1].npc, "GH", 37 };
    ViewEncoder encoder;
    View received;
    assert(apply_view(game_data, encode_view(game_data, sent, encoder), received));
    assert(received.merc && received.merc->npc == &game_data.mercs[1].npc);
    assert(received.merc->mode == "GH" && received.merc->fhr == 37);
    std::printf("OK: the merc's FHR round-trips the view codec\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
