// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A fresh character's start items are made in its first game (FUN_00569f80 -> FUN_00534f10): after the
// player's seed step, each takes two game seed steps (FUN_00559ce0 -> FUN_00558d90: its unit seed, then its
// own), normal at ilvl 1, with FUN_00534c70's record bits 8 (no sockets) and 2 (no ethereal); then the first
// slot's StartSkill stat and full stacks. The record bits on their own: with them a normal item never sockets
// or goes ethereal; without, some do. Needs the game data.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <gamedata_load.hpp>
#include <item_gen.hpp>
#include <rules.hpp>
#include <spawning.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

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
    // The record bits: a short sword (normal, ilvl 30: up to two sockets) over many seeds.
    int socketed = 0, ethereal = 0;
    for (std::uint32_t seed = 1; seed <= 3000; ++seed) {
        d2d::rules::Rng unit{ seed * 7919u }, own{ seed };
        const auto plain = d2d::rules::generate_item(data->rules, "ssd", 30, 2, own, &unit);
        socketed += plain.socketed; ethereal += plain.ethereal;
        d2d::rules::Rng unit2{ seed * 7919u }, own2{ seed };
        const auto start = d2d::rules::generate_item(data->rules, "ssd", 30, 2, own2, &unit2, nullptr, false, 0, 0, false, d2d::rules::kNoEthereal | d2d::rules::kNoSockets);
        assert(!start.socketed && !start.ethereal);
    }
    std::printf("record bits: without them %d of 3000 socketed, %d ethereal; with them none\n", socketed, ethereal);
    assert(socketed > 0 && ethereal > 0);
    for (int cls = 0; cls < 7; ++cls) {
        d2d::rules::Rng rng(7);
        auto made = new_character(*data, cls, "Fresh", false, true, rng);
        Character character;
        character.character_class = cls;
        character.name = "Fresh";
        character.header = made.header;
        character.stats = made.stats;
        character.items = made.items;
        character.fresh = true;
        World world(&*data, -1, -1);
        world.enter(character);
        auto expected = start_spawning(*data, character.header.active_difficulty()).game;
        assert(world.actor->seed.low == expected.next());
        const auto& items = world.actor->character.items;
        assert(items.size() == made.items.size());
        for (std::size_t k = 0; k < items.size(); ++k) {
            const auto unit = expected.next();
            expected.next();
            assert(items[k].uid == unit && items[k].code == made.items[k].code);
            assert(!items[k].socketed && !items[k].ethereal && items[k].quality == 2);
            assert(items[k].location == made.items[k].location && items[k].quantity == made.items[k].quantity);
            assert(items[k].durability == items[k].max_durability);
        }
        assert(world.fight.spawning.game.low == expected.low && world.fight.spawning.game.high == expected.high);
        if (cls == d2d::d2s::kSorceress) {
            bool fire_bolt = false;
            for (const auto& prop : items.front().props) fire_bolt = fire_bolt || (prop.stat == d2d::d2s::kSingleSkill && prop.param == 36);
            assert(fire_bolt);
        }
    }
    std::printf("OK: each class's start items made on the game seed after the player's, normal, unsocketed, not ethereal\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_start_items: %s\n", error.what());
    return 1;
}
