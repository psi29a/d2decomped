// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A player's unit seed (Fight::Hero::seed) is a game seed step at its join
// (player_join_init FUN_00569f20: FUN_00552df0 with EDX game +0xd0, 0x569f4b),
// then each item record its save holds (its list, a socketed item each a
// record, the corpse's and the merc's) takes two as FUN_00558cb0 makes it a
// unit (FUN_00555230 type 4). Needs the game data.

#include <character.hpp>
#include <character_store.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <spawning.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng(7);
    auto made = new_character(*data, 4, "Rite", false, true, rng);
    Character character;
    character.character_class = 4;
    character.name = "Rite";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    assert(!character.items.empty());
    // Its first item with one socketed in it: two records.
    character.items.front().socketed_items.push_back(character.items.back());

    // The first player: the step right after the game's own (start_spawning).
    World world(&*data, -1, -1);
    world.enter(character);
    auto fresh = start_spawning(*data, character.header.active_difficulty());
    const auto first = fresh.game.next();
    assert(world.actor->seed.low == first && world.actor->seed.high == 666);
    std::printf("OK: the first player's seed is the game seed's first step after its own\n");

    // A second player joining the game under way: its step, then two a record.
    const auto records = character.items.size() + 1;
    auto expected = world.fight.spawning.game;
    const auto second = expected.next();
    for (std::size_t record = 0; record < records; ++record) { expected.next(); expected.next(); }
    auto& joined = world.join(character);
    assert(joined.seed.low == second && joined.seed.high == 666);
    assert(world.fight.spawning.game.low == expected.low && world.fight.spawning.game.high == expected.high);
    std::printf("OK: a joining player's seed, then two game seed steps for each of its %zu item records\n", records);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_player_join_seed: %s\n", error.what());
    return 1;
}
