// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Hellforge opens (a4q3 state 0 -> 1, LAB_005b5a90) when the Hellforge
// Hammer, the item bound to its record (Items.txt quest 25), goes into the
// inventory; the soulstone (quest 23, a4q1's) doesn't
// (tools/emu/quest_item_events.py).

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
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    const auto pick = [&](const std::string& code) {
        d2d::d2s::Item item;
        item.code = code; item.identified = true;
        item.location = d2d::d2s::item_location::kStored; item.panel = d2d::d2s::item_panel::kInventory;
        item.column = 9; item.row = 3;
        world.actor->character.items.push_back(item);
        world.act4_update(0);
    };
    world.hellforge.open = true; world.hellforge.state = 0;
    pick(d2d::rules::HellforgeQuest::kStone);
    assert(world.hellforge.state == 0);                                           // the soulstone: a4q1's, not this record's
    pick(d2d::rules::HellforgeQuest::kHammer);
    assert(world.hellforge.state == 1);                                           // the hammer: open
    std::printf("OK: the Hellforge opens on the hammer, not the soulstone\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
