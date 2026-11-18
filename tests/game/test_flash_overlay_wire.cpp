// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A unit's one-shot overlay (a skill start's castoverlay, FUN_004c6140; a
// monster think's, FUN_00621e40) crosses the view codec. Every View goes
// through it, a local game's too (Town::publish), so without it no flash
// ever reached the draw.

#include <ai.hpp>
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
    game_data.monsters.types.resize(1);   // a monster's look names its MonStats row
    View sent;
    sent.level = &game_data.town;
    sent.flash_overlay = 29; sent.flash_at = 2872;   // Fire Bolt's fire_cast_1 on the player
    sent.merc = View::Merc{ {}, &game_data.mercs[1].npc, "SC", 0, 30, 3000 };
    View::Pet pet{};
    pet.flash_overlay = 31; pet.flash_at = 3100;
    sent.pets.push_back(pet);
    sent.party_pets.push_back(pet);
    View::OtherPlayer other;
    other.flash_overlay = 32; other.flash_at = 3200;
    sent.others.push_back(other);
    Monster monster;
    monster.id = 7; monster.type = 0; monster.flash_overlay = 33; monster.flash_at = 3300;
    sent.monsters.push_back(monster);

    ViewEncoder encoder;
    View received;
    assert(apply_view(game_data, encode_view(game_data, sent, encoder), received));
    assert(received.flash_overlay == 29 && received.flash_at == 2872);
    assert(received.merc && received.merc->flash_overlay == 30 && received.merc->flash_at == 3000);
    assert(received.pets.size() == 1 && received.pets[0].flash_overlay == 31 && received.pets[0].flash_at == 3100);
    assert(received.party_pets.size() == 1 && received.party_pets[0].flash_overlay == 31);
    assert(received.others.size() == 1 && received.others[0].flash_overlay == 32 && received.others[0].flash_at == 3200);
    assert(received.monsters.size() == 1 && received.monsters[0].flash_overlay == 33 && received.monsters[0].flash_at == 3300);

    // The next View, the flash over: a delta carries the -1.
    sent.flash_overlay = -1;
    sent.monsters[0].flash_overlay = -1;
    assert(apply_view(game_data, encode_view(game_data, sent, encoder), received));
    assert(received.flash_overlay == -1 && received.monsters.size() == 1 && received.monsters[0].flash_overlay == -1);
    std::printf("OK: one-shot overlays round-trip the view codec\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
