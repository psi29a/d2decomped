// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Charsi's imbue answered (FUN_00579d60, C->S 0x38 kind 0): not the cursor
// item, no answer (FUN_00578610); refused, S->C 0x58 kind 7 (no quest bit
// 0x579fc6, not imbuable 0x57a002); done, kind 6 (0x57a1b4), the reward
// used. ev::ServiceResult carries it, and survives the view codec.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <monster_ids.hpp>
#include <protocol.hpp>
#include <quests.hpp>
#include <replication.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    {   // The view codec carries it (event 10).
        GameData empty_data;
        View sent;
        sent.level = &empty_data.town;
        sent.events = { ev::ServiceResult{ 4, true }, ev::ServiceResult{ 7, false } };
        ViewEncoder encoder;
        View received;
        assert(apply_view(empty_data, encode_view(empty_data, sent, encoder), received));
        assert(received.events.size() == 2);
        const auto& done = std::get<ev::ServiceResult>(received.events[0]);
        const auto& refused = std::get<ev::ServiceResult>(received.events[1]);
        assert(done.npc == 4 && done.done && refused.npc == 7 && !refused.done);
        std::printf("OK: ServiceResult round-trips the view codec\n");
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1);

    d2d::rules::Rng rng(7);
    constexpr int kBarbarian = 4;
    auto made = new_character(*data, kBarbarian, "Smith", false, true, rng);
    Character character;
    character.character_class = kBarbarian;
    character.name = "Smith";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    const auto& npcs = world.level->npcs;
    const int charsi = int(std::ranges::find(npcs, d2d::rules::monster_ids::kCharsi, &Npc::hc_idx) - npcs.begin());
    assert(std::size_t(charsi) < npcs.size());
    auto& quest_bits = world.actor->character.header.quests[0];
    constexpr int kTools = d2d::rules::ToolsQuest::kQuest;
    const auto hand = [&](int quality, int item_id) {
        auto axe = d2d::rules::generate_item(data->rules, "axe", 8, quality, rng);
        axe.id = item_id;
        axe.identified = true;
        world.actor->held = axe;
        world.actor->events.clear();
    };
    const auto answer = [&]() -> const ev::ServiceResult* {
        for (const auto& event : world.actor->events)
            if (const auto* result = std::get_if<ev::ServiceResult>(&event)) return result;
        return nullptr;
    };

    // Not the cursor item's id: nothing happens, nothing answers.
    d2d::rules::quest::set(quest_bits, kTools, 1);
    hand(2, 9100);
    world.apply(cmd::Imbue{ charsi, 9101 }, 27 * kTickMs);
    assert(!answer() && world.actor->held->quality == 2 && d2d::rules::quest::bit(quest_bits, kTools, 1));
    // A rare isn't imbuable (FUN_0062c590): refused.
    hand(6, 9102);
    world.apply(cmd::Imbue{ charsi, 9102 }, 27 * kTickMs);
    assert(answer() && answer()->npc == charsi && !answer()->done && world.actor->held->id == 9102);
    // A normal axe with the reward due: done, made rare, the reward used.
    hand(2, 9103);
    world.apply(cmd::Imbue{ charsi, 9103 }, 27 * kTickMs);
    assert(answer() && answer()->npc == charsi && answer()->done && !world.actor->held);
    // Off the cursor into the inventory (FUN_00560200).
    const auto& stored = world.actor->character.items.back();
    assert(stored.code == "axe" && stored.quality == 6 && stored.location == d2d::d2s::item_location::kStored && stored.panel == d2d::d2s::item_panel::kInventory);
    assert(d2d::rules::quest::bit(quest_bits, kTools, 0) && !d2d::rules::quest::bit(quest_bits, kTools, 1));
    // Used: the next is refused.
    hand(2, 9104);
    world.apply(cmd::Imbue{ charsi, 9104 }, 27 * kTickMs);
    assert(answer() && !answer()->done && world.actor->held->quality == 2);
    // No room: on the ground at the player (FUN_00555da0 / FUN_00558aa0).
    auto& items = world.actor->character.items;
    std::erase_if(items, [](const auto& item) { return item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory; });
    for (int cell = 0; cell < 40; ++cell) {
        auto potion = d2d::rules::generate_item(data->rules, "hp1", 1, 2, rng);
        potion.location = d2d::d2s::item_location::kStored; potion.panel = d2d::d2s::item_panel::kInventory; potion.column = cell % 10; potion.row = cell / 10;
        items.push_back(std::move(potion));
    }
    d2d::rules::quest::set(quest_bits, kTools, 1);
    const std::size_t on_ground = world.loot.ground.size(), carried = items.size();
    hand(2, 9105);
    world.apply(cmd::Imbue{ charsi, 9105 }, 27 * kTickMs);
    assert(answer() && answer()->done && !world.actor->held && items.size() == carried);
    assert(world.loot.ground.size() == on_ground + 1 && world.loot.ground.back().item.quality == 6);
    std::printf("OK: Charsi's imbue answers 0x58 done / refused, nothing for another item\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_imbue_reply: %s\n", error.what());
    return 1;
}
