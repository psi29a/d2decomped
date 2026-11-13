// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Countess's treasure (FUN_005954f0): each towerchestspawner (Missiles.txt
// row 332, Range 400, LevRange 1, made at level 1) lives 401 frames, so its
// chest opens at Range - Param1 left on the 152nd frame (FUN_005af300).

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <cues.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <object_ids.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <variant>

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
    using Tower = d2d::rules::TowerQuest;
    const auto* cellar = data->level(Tower::kCellar);
    if (!cellar) { std::printf("SKIP: no Tower Cellar Level 5\n"); return D2D_TEST_SKIPPED; }

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
    populate_level(*data, world.fight.spawning, *cellar);
    world.level = cellar;
    world.npc_states = npc_start(*cellar);
    world.tower.dead = true;
    const auto chest = std::ranges::find_if(cellar->npcs, [](const auto& npc) {
        return npc.root == "objects" && npc.object_id == d2d::rules::object_ids::kLargeChestR;
    });
    if (chest == cellar->npcs.end()) { std::printf("SKIP: no chest in cellar 5\n"); return D2D_TEST_SKIPPED; }
    // InitFn 47 (FUN_00595a50) lists a chest as its room comes up: dead before
    // any is up, nothing yet; the first chest up makes the treasure there.
    world.objects_inited[cellar] = std::size_t(chest - cellar->npcs.begin());
    world.actor->events.clear();
    world.countess_died(0);
    assert(world.treasure.empty() && !world.tower.treasure);
    // Unit event 0x25 (0x4cbaad): the music off for 1500 ticks, for a player who took part.
    const auto hushes = [&](int back_ms) {
        return std::ranges::count_if(world.actor->events, [&](const Event& event) { const auto* music = std::get_if<ev::MusicCue>(&event); return music && music->sound == 0 && music->back_ms == back_ms; });
    };
    assert(world.actor->tower_line_held && hushes(1500 * 40) == 1);
    auto game_seed = world.fight.spawning.game;
    world.init_new_objects();
    assert(world.treasure.size() == 1 && world.tower.treasure);
    const auto spawner_seed = d2d::rules::Rng{ game_seed.next() };   // its unit seed: a game seed step (FUN_00552df0)
    assert(world.treasure.front().seed.low == spawner_seed.low && world.treasure.front().seed.high == 666);
    world.objects_inited[cellar] = 0;                               // the ones before it too: still that one
    world.init_new_objects();
    assert(world.treasure.size() == 1);
    const auto opened = [&]() { return world.operated.contains({ cellar, world.treasure.front().npc }); };
    for (int frame = 0; frame < 151; ++frame) world.tower_treasure(0);
    assert(!opened());
    world.tower_treasure(0);
    assert(opened());
    std::printf("OK: only chests up when it's made get spawners; the chest opens on the 152nd frame\n");

    // Her line waits for the treasure's end (unit event 0x5c at 1 left,
    // FUN_005af300 -> FUN_004dce10): 1 s after it, within 60 s of her death.
    const int line = data->sound_index.at("sorceress_act1_complete_tower");
    const auto lines = [&](std::uint32_t at_ms) { return std::ranges::count_if(world.cues.due, [&](const Cues::Cue& cue) { return cue.sound == line && cue.when_ms == at_ms; }); };
    assert(lines(0) == 0);                               // not at her death
    world.actor->tower_line_held = 0;
    world.cues.due.clear();
    while (world.treasure.front().left > 1) world.tower_treasure(30'000);
    assert(lines(31'000) == 0);
    world.tower_treasure(30'000);                        // 1 left: the 0x5c
    assert(lines(31'000) == 1 && !world.actor->tower_line_held);
    assert(hushes(75 * 40) == 1);                        // the music back 0x32 ticks after the line (FUN_004dce10)
    world.treasure.front().left = 1;                     // over 60 s on: dropped
    world.actor->tower_line_held = 0;
    world.cues.due.clear();
    world.tower_treasure(60'000);
    assert(lines(61'000) == 0 && !world.actor->tower_line_held);
    assert(hushes(75 * 40) == 1);                        // no second one
    std::printf("OK: her complete_tower line plays 1 s after the treasure's end, within 60 s\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
