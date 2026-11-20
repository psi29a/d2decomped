// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Who a slain player was killed by (FUN_00580ec0's killer, FUN_0054d8a0's
// event 6, named as the client's FUN_0049eb10 does): a monster's MonStats
// NameStr, a SuperUnique's name, "an Exploding Barrel" (0xc98) for object
// 0xb, another object's Objects.txt Name; none, no message. A blow carries
// its striker to Foe::striker_id, and the player's death keeps the last one as the
// killer (Fight::die). With two strikers in a frame the killer is the one whose
// record took the life under one point (FUN_0057c6c0, one record at a time), not
// the last. Needs the game data.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

namespace filesystem = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const filesystem::path data_dir = mpq_dir ? filesystem::path(mpq_dir) : filesystem::path(home ? home : "") / "Workspace/private/diablo2";
    if (!filesystem::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? filesystem::path(patch) : filesystem::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng(7);
    auto made = new_character(*data, 1, "Slain", false, true, rng);
    Character character;
    character.character_class = 1;
    character.name = "Slain";
    character.header = std::move(made.header);
    character.stats = std::move(made.stats);
    character.items = std::move(made.items);
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    const auto fallen = data->monsters.by_id.find("fallen1");
    assert(fallen != data->monsters.by_id.end());
    d2d::rules::Rng spawn_rng{ 5 };
    auto monster = make_monster(*data, int(fallen->second), world.actor->player.x + 1, world.actor->player.y, spawn_rng, 0);
    monster.id = 4242;
    world.fight.monsters.push_back(monster);
    using Striker = Fight::Hero::Striker;
    assert(world.killer_name(Striker{}).empty());                      // none: no message
    assert(world.killer_name({ 1, 4242 }) == "Fallen");
    auto bishibosh = std::move(monster);
    bishibosh.id = 4243;
    bishibosh.super = 1;
    world.fight.monsters.push_back(std::move(bishibosh));
    assert(world.killer_name({ 1, 4243 }) == data->superuniques[1].name);
    assert(world.killer_name({ 2, 0xb }) == "an Exploding Barrel");
    assert(!world.killer_name({ 2, 3 }).empty());                       // an Objects.txt row's Name
    // A blow's striker reaches the death: Fallen's lethal blow makes it the killer.
    Foe foe;
    d2d::rules::Taken taken;
    taken.hit = true;
    taken.damage = 1;
    foe.take(taken, 4242);
    assert(foe.striker_id == 4242);
    world.actor->struck_by = { 1, foe.striker_id };
    world.fight.die(0);
    assert(world.actor->killer.kind == 1 && world.actor->killer.id == 4242 && world.actor->struck_by.kind == -1);
    // Two strikers in one frame: Fallen's blow takes the last 10 points, Bishibosh's lands after it.
    world.enter(character);
    world.actor->character.stats.values[d2d::d2s::kLife] = 10 << 8;
    std::array<Foe, 2> side{};
    d2d::rules::Taken lethal;
    lethal.hit = true;
    lethal.damage = 12;
    side[0].take(lethal, 4242);
    d2d::rules::Taken after = lethal;
    after.damage = 3;
    side[0].take(after, 4243);
    assert(side[0].striker_id == 4243);                              // the frame's last blow
    world.fight.hurt_side(side, 1000);
    assert(world.fight.dead() && world.actor->killer.kind == 1 && world.actor->killer.id == 4242);
    std::printf("slain message: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_slain_message: %s\n", error.what());
    return 1;
}
