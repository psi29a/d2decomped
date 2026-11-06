// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Lam Esen's Tome (bbb, bound to a3q1) picked up / dropped moves the quest
// to state 4 / 3 whatever its state (FUN_005b7970 / LAB_005b7a00, events 4 /
// 5; tools/emu/quest_item_events.py).

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <quests_act3.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
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
    auto& items = world.actor->character.items;
    d2d::d2s::Item tome;
    tome.code = d2d::rules::LamEsenQuest::kTome; tome.identified = true;
    tome.location = d2d::d2s::item_location::kStored; tome.panel = d2d::d2s::item_panel::kInventory;
    tome.column = 9; tome.row = 3;
    world.lam_esen.open = true; world.lam_esen.state = 1;                        // given out, not yet under way
    items.push_back(tome);
    world.act3_update(0);
    assert(world.lam_esen.state == 4);                                            // picked up: 4 from any state
    std::erase_if(items, [](const d2d::d2s::Item& item) { return item.code == d2d::rules::LamEsenQuest::kTome; });
    world.act3_update(0);
    assert(world.lam_esen.state == 3);                                            // dropped: 3
    world.lam_esen.open = false;
    items.push_back(tome);
    world.act3_update(0);
    assert(world.lam_esen.state == 3);                                            // shut (+9 clear): nothing
    std::printf("OK: the tome's pickup and drop move a3q1 whatever its state\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
