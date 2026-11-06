// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Khalim's chests (OperateFn 58, FUN_005b8940): after the gold, one part
// for each player in the game carrying neither it nor Khalim's Will
// (LAB_005b8810 counts them into a3q2's data +0xc); no quest bit is read.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <object_ids.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string_view>
#include <utility>

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
    const auto* cavern = data->level(d2d::rules::level_ids::kSpiderCavern);
    if (!cavern) { std::printf("SKIP: no Spider Cavern\n"); return D2D_TEST_SKIPPED; }
    using Khalim = d2d::rules::KhalimQuest;

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
    populate_level(*data, world.fight.spawning, *cavern);
    const auto chest = std::ranges::find(cavern->npcs, d2d::rules::operate_fn::kKhalimEye, &Npc::operate_fn);
    if (chest == cavern->npcs.end()) { std::printf("SKIP: no Eye chest in the Spider Cavern once populated\n"); return D2D_TEST_SKIPPED; }
    world.level = cavern;
    world.npc_states = npc_start(*cavern);
    d2d::rules::quest::set(world.quests(), Khalim::kQuest, 0);       // the opener's done: no quest bit is read
    auto& second = world.add_player();
    second.character = character;
    World::Player& opener = *world.actor;
    const int chest_index = int(chest - cavern->npcs.begin());
    // Each player carrying `codes` ("" none), the chest opened afresh: the eyes dropped.
    const auto eyes = [&](const char* opener_code, const char* second_code) {
        for (auto [player, code] : { std::pair{ &opener, opener_code }, std::pair{ &second, second_code } }) {
            auto& items = player->character.items;
            std::erase_if(items, [](const d2d::d2s::Item& item) { return item.code == Khalim::kEyeCode || item.code == Khalim::kWillCode; });
            d2d::d2s::Item part;
            part.code = code;
            if (!part.code.empty()) items.push_back(std::move(part));
        }
        world.operated.erase({ cavern, chest_index });
        world.loot.ground.clear();
        world.operate(chest_index, 0);
        return std::ranges::count(world.loot.ground, std::string_view(Khalim::kEyeCode), [](const auto& ground) { return std::string_view(ground.item.code); });
    };
    assert(eyes("", "") == 2);                                       // one each
    assert(eyes("", Khalim::kEyeCode) == 1);                         // the second has it
    assert(eyes(Khalim::kEyeCode, "") == 1);                         // the opener has it
    assert(eyes(Khalim::kWillCode, "") == 1);                        // the Will counts as having it
    assert(eyes(Khalim::kEyeCode, Khalim::kWillCode) == 0);
    std::printf("OK: the Eye chest drops one eye per player carrying neither it nor the Will\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
