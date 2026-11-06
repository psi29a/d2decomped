// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Gibbet with no room for Cain (FUN_00593290): a town portal pair
// (FUN_0056d130, object 0x3b) opens at x + 6, y + 6, linked only to its
// twin in the camp; nobody's own, so the opener's own town portal stays.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <monster_ids.hpp>
#include <object_ids.hpp>
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
    const auto* tristram = data->level(d2d::rules::CainQuest::kTristram);
    if (!tristram) { std::printf("SKIP: no Tristram\n"); return D2D_TEST_SKIPPED; }

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
    populate_level(*data, world.fight.spawning, *tristram);
    Level no_room = *tristram;                                       // Cain had no spot: not made
    std::erase_if(no_room.npcs, [](const Npc& npc) { return npc.hc_idx == d2d::rules::monster_ids::kCain; });
    const auto gibbet = std::ranges::find(no_room.npcs, d2d::rules::operate_fn::kGibbet, &Npc::operate_fn);
    if (gibbet == no_room.npcs.end()) { std::printf("SKIP: no Gibbet in Tristram once populated\n"); return D2D_TEST_SKIPPED; }
    world.level = &no_room;
    world.npc_states = npc_start(no_room);
    world.cain.state = 5;
    const World::Portal own{ &data->town, 10, 10, 1 };               // the opener's own portal, open elsewhere
    world.portal[world.actor->portal_pair] = own;
    world.cain_operate(int(gibbet - no_room.npcs.begin()), 1000);
    assert(world.portal[world.actor->portal_pair].level == own.level && world.portal[world.actor->portal_pair].born == own.born);
    assert(world.portal[World::kGibbetPortal].level == &no_room && world.portal[World::kGibbetPortal + 1].level == &data->town);
    std::printf("OK: the Gibbet's portal is a pair of its own; the opener's stays\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
