// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// S->C 0x89 {event} as ev::UniqueEvent (FUN_00545760 to every client; 3, the
// staff in the orifice, FUN_0059dd80 at 0x59de02) survives the view codec as
// its event 13. No game data needed.

#include <gamedata.hpp>
#include <replication.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    GameData empty_data;
    View sent;
    sent.level = &empty_data.town;
    sent.events = { ev::UniqueEvent{ 3 } };
    ViewEncoder encoder;
    View received;
    assert(apply_view(empty_data, encode_view(empty_data, sent, encoder), received));
    assert(received.events.size() == 1 && std::get<ev::UniqueEvent>(received.events[0]).event == 3);
    std::printf("OK: S->C 0x89 {3} through the view codec\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_unique_event: %s\n", error.what());
    return 1;
}
