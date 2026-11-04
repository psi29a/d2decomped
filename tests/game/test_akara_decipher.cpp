// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Akara deciphers the Bark Scroll (FUN_00592250, dialog 0x70): the scroll
// goes (FUN_00544160) and a new bkd is made (FUN_005466b0), placed in the
// inventory's first free spot (FUN_005600a0), not where the scroll lay.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
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
    d2d::d2s::Item scroll;
    scroll.code = "bks"; scroll.identified = true; scroll.quality = 2;
    scroll.location = d2d::d2s::item_location::kStored; scroll.panel = d2d::d2s::item_panel::kInventory;
    scroll.column = 8; scroll.row = 2;                                            // far off, the inventory otherwise empty
    character.items.push_back(scroll);
    character.panel = panel_stats(*data, character.header, character.items, character.stats);

    World world(&*data, -1, -1);
    world.enter(character);
    world.cain.state = 4;                                                         // the scroll found, Akara asked
    world.act1_said(d2d::rules::CainQuest::kAkara, 112);
    const auto& items = world.actor->character.items;
    assert(std::ranges::none_of(items, [](const auto& item) { return item.code == "bks"; }));
    const auto bkd = std::ranges::find(items, std::string_view("bkd"), &d2d::d2s::Item::code);
    assert(bkd != items.end() && bkd->panel == d2d::d2s::item_panel::kInventory);
    assert(bkd->column == 0 && bkd->row == 0);                                    // a new item in the first free spot
    std::printf("OK: Akara's bkd is a new item in the first free spot\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
