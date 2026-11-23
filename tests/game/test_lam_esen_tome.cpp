// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Lam Esen's Tome (bbb, bound to a3q1) into the inventory / dropped moves the
// quest to state 4 / 3 whatever its state and puts the player on / off its
// holders list (FUN_005b7970 / LAB_005b7a00, events 4 / 5; tools/emu/
// quest_item_events.py), through the real World's events: item_event and a
// cmd::Drop of the tome in hand. Shut (+9 clear), only the list's removal.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <protocol.hpp>
#include <quests_act3.hpp>
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
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    auto* actor = world.actor;
    d2d::d2s::Item tome;
    tome.code = d2d::rules::LamEsenQuest::kTome; tome.identified = true; tome.id = 77;
    world.lam_esen.open = true; world.lam_esen.state = 1;                        // given out, not yet under way
    world.item_event(tome.code, 1000);                                           // event 4: into the inventory
    assert(world.lam_esen.state == 4 && world.lam_esen.holders_list.has(actor->id));   // 4 from any state, a holder
    const auto drop = [&] {                                                      // the tome in hand, dropped (C->S 0x17)
        actor->held = tome;
        const Order order{ actor->id, cmd::Drop{ tome.id } };
        world.tick({ &order, 1 }, 2000, 1960);
    };
    drop();                                                                      // event 5 (0x543db0)
    assert(!actor->held && world.lam_esen.state == 3 && !world.lam_esen.holders_list.has(actor->id));
    world.lam_esen.open = false;                                                 // shut (+9 clear)
    world.item_event(tome.code, 3000);
    assert(world.lam_esen.state == 3 && !world.lam_esen.holders_list.has(actor->id));   // event 4: nothing
    world.lam_esen.holders_list.add(actor->id);
    drop();
    assert(world.lam_esen.state == 3 && !world.lam_esen.holders_list.has(actor->id));   // event 5: off the list still
    std::printf("OK: the tome's item events move a3q1 whatever its state, and its holders list\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
