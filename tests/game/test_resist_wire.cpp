// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's difficulty and its boss mods' resist adds cross the view codec,
// so the client's hover bar shows the immunities the host's monster has
// (rules::immunity_lines over Monster::target).

#include <ai.hpp>
#include <gamedata.hpp>
#include <montypes.hpp>
#include <replication.hpp>
#include <uniques.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    GameData game_data;
    d2d::rules::MonType fallen;
    fallen.killable = true;
    fallen.diff[2].res = { 15, 0, 100, 0, 40, 0 };                                   // fallen1's Hell resists: fire immune
    game_data.monsters.types = { fallen };
    game_data.mon_npc.resize(1);
    View sent;
    sent.level = &game_data.town;
    Monster monster;
    monster.id = 7; monster.type = 0; monster.hit_points = 10; monster.mode = "NU";
    monster.difficulty = 2;
    monster.boss_res = { 0, 0, 0, 75, 0, 0 };                                        // lightning enchanted's +75
    sent.monsters.push_back(monster);
    ViewEncoder encoder;
    View received;
    assert(apply_view(game_data, encode_view(game_data, sent, encoder), received));
    assert(received.monsters.size() == 1 && received.monsters[0].difficulty == 2 && received.monsters[0].boss_res[3] == 75);
    const auto lines = d2d::rules::immunity_lines("fallen1", received.monsters[0].target(game_data).res);
    assert(lines.size() == 1 && lines[0].stat == 39);                                // fire only: lightning 75 isn't over 99
    std::printf("OK: a monster's resists cross the view codec\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
