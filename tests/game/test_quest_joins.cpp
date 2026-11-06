// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The quests' joins (FUN_00546270, quests.md "Joins"): the game's first
// player runs every +0xd4 unless made new (`fresh`); a later player (World::
// join) runs only the +0xd8s (join_more). Expected values: tools/emu/
// quest_joins.py (plain: +0xd4, `--d8`: +0xd8).

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <quests_act4.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1);
    if (!data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

    d2d::rules::Rng rng(7);
    const auto make = [&](int cls, const char* name) {
        auto made = new_character(*data, cls, name, false, true, rng);
        Character character;
        character.character_class = cls;
        character.name = name;
        character.header = made.header;
        character.stats = made.stats;
        character.items = made.items;
        character.panel = panel_stats(*data, character.header, character.items, character.stats);
        return character;
    };
    constexpr int kSorceress = 1, kBarbarian = 4;
    {   // a4q3's +0xd4 (FUN_005b64e0): no soulstone, state 1; a character made new runs none.
        auto loaded = make(kSorceress, "Loaded");
        World world(&*data, -1, -1);
        world.enter(loaded);
        assert(world.hellforge.state == 1);
        auto fresh = make(kSorceress, "Fresh");
        fresh.fresh = true;
        world.enter(fresh);
        assert(world.hellforge.state == 0);
    }
    (void)kBarbarian;
    std::printf("OK: quest joins\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
