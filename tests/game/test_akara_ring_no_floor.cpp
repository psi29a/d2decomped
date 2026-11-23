// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Akara's ring (FUN_00592250, string 118 -> FUN_005466b0 -> FUN_00555da0): a
// put that finds no floor makes no ring; d2d then leaves the claim open (quest
// bits, rewarded, state), so a later talk can still give it.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <quests.hpp>
#include <rules.hpp>
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
    assert(data && data->town.id == 1);
    if (!data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Headless", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    std::erase_if(character.items, [](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory; });
    character.panel = panel_stats(*data, character.header, character.items, character.stats);

    World world(&*data, -1, -1);
    world.enter(character);
    world.cain.state = 4;
    d2d::rules::quest::set(world.quests(), d2d::rules::CainQuest::kQuest, 1);     // Akara's reward is due
    world.actor->player.x = -4000.f;                                              // off every room: no floor
    world.actor->player.y = -4000.f;
    const auto ground_before = world.loot.ground.size();
    world.act1_said(d2d::rules::CainQuest::kAkara, 118);
    assert(world.loot.ground.size() == ground_before);
    assert(d2d::rules::quest::bit(world.quests(), d2d::rules::CainQuest::kQuest, 1));
    assert(!d2d::rules::quest::bit(world.quests(), d2d::rules::CainQuest::kQuest, 0));
    assert(world.cain.state == 4 && !world.cain.rewarded.has(world.actor->id));
    std::printf("OK: a ring with no floor leaves Akara's reward claimable\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
