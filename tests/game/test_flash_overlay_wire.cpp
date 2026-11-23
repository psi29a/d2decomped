// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A unit's one-shot overlay (a skill start's castoverlay, FUN_004c6140; a
// monster think's, FUN_00621e40) crosses the view codec. Every View goes
// through it, a local game's too (ClientSession::publish), so without it no flash
// ever reached the draw.

#include <ai/units.hpp>
#include <gamedata.hpp>
#include <overlays.hpp>
#include <replication.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::game;

int main() try {
    GameData game_data;
    game_data.mercs[1];
    game_data.monsters.types.resize(1);   // a monster's look names its MonStats row
    View sent;
    sent.level = &game_data.town;
    using d2d::rules::OneShots;
    sent.one_shots = { { 29, 2872 }, { 0x2e, 2800 } };   // Fire Bolt's fire_cast_1 on the player, beside a Cry Help flash
    sent.merc = View::Merc{ {}, &game_data.mercs[1].npc, "SC", 0, { { 30, 3000 } } };
    View::Pet pet{};
    pet.one_shots = { { 31, 3100 } };
    sent.pets.push_back(pet);
    sent.party_pets.push_back(pet);
    View::OtherPlayer other;
    other.one_shots = { { 32, 3200 } };
    sent.others.push_back(other);
    Monster monster;
    monster.id = 7; monster.type = 0; monster.one_shots = { { 33, 3300 } };
    sent.monsters.push_back(std::move(monster));

    ViewEncoder encoder;
    View received;
    assert(apply_view(game_data, encode_view(game_data, sent, encoder), received));
    assert(received.one_shots == sent.one_shots);
    assert(received.merc && (received.merc->one_shots == OneShots{ { 30, 3000 } }));
    assert(received.pets.size() == 1 && received.pets[0].one_shots == pet.one_shots);
    assert(received.party_pets.size() == 1 && received.party_pets[0].one_shots == pet.one_shots);
    assert(received.others.size() == 1 && received.others[0].one_shots == other.one_shots);
    assert(received.monsters.size() == 1 && (received.monsters[0].one_shots == OneShots{ { 33, 3300 } }));

    // The next View, the list emptied: a delta carries it.
    sent.one_shots.clear();
    sent.monsters[0].one_shots.clear();
    assert(apply_view(game_data, encode_view(game_data, sent, encoder), received));
    assert(received.one_shots.empty() && received.monsters.size() == 1 && received.monsters[0].one_shots.empty());
    std::printf("OK: one-shot overlays round-trip the view codec\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
