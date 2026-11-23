// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A Reanimated Horde in its rise's S1 (Monster::rising, MonUMod 34) crosses the view codec, in a keyframe
// and in an update, so a joined game does not offer it as a target while the host refuses it.

#include <ai/units.hpp>
#include <gamedata.hpp>
#include <montypes.hpp>
#include <replication.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::game;

int main() try {
    GameData game_data;
    d2d::rules::MonType horde;
    horde.killable = true;
    game_data.monsters.types = { std::move(horde) };
    game_data.mon_npc.resize(1);
    View sent;
    sent.level = &game_data.town;
    Monster monster;
    monster.id = 7; monster.type = 0; monster.hit_points = 10; monster.mode = "S1"; monster.rising = true;
    monster.walk_pct = 40;                                                              // its walk % (protocol 10)
    sent.monsters.push_back(std::move(monster));
    ViewEncoder encoder;
    View received;
    assert(apply_view(game_data, encode_view(game_data, sent, encoder), received));   // the keyframe
    assert(received.monsters.size() == 1 && received.monsters[0].rising && !received.monsters[0].targetable());
    assert(received.monsters[0].walk_pct == 40);
    sent.monsters[0].rising = false;                                                    // an update: it clears
    assert(apply_view(game_data, encode_view(game_data, sent, encoder), received));
    assert(received.monsters.size() == 1 && !received.monsters[0].rising && received.monsters[0].targetable());
    std::printf("OK: a rising Horde's flag and walk %% round-trip the view codec\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
