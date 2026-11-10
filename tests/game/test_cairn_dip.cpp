// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Cairn Stones open (FUN_00593710): the way to Tristram, then S->C 0x89
// event 1, whose client entry (0x46aee0) dips the ambient to black from
// missile cairnstones: Param1 (40) in and back, Range (300) less twice that
// (220) held, in Stony Field (rules::cairn_dip, lighting.md).

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
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
    const auto stones = data->missiles.find("cairnstones");
    assert(stones != data->missiles.end() && stones->second.param1 == 40 && stones->second.range == 300);
    const auto* stony_field = data->level(d2d::rules::level_ids::kStonyField);
    if (!stony_field) { std::printf("SKIP: no Stony Field\n"); return D2D_TEST_SKIPPED; }

    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Stones", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Stones";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);

    World world(&*data, -1, -1);
    world.enter(character);
    populate_level(*data, world.fight.spawning, *stony_field);
    world.level = stony_field;
    world.npc_states = npc_start(*stony_field);
    world.cain.active = true;
    world.actor->character.items.push_back({ .code = "bkd" });      // the deciphered scroll
    world.cain.stone_order(world.fight.spawning.quests.low, world.fight.spawning.quests.high);
    world.actor->events.clear();
    for (int step = 0; step < 5; ++step) {
        const auto stone = std::ranges::find(stony_field->npcs, world.cain.order[std::size_t(step)], &Npc::object_id);
        if (stone == stony_field->npcs.end()) { std::printf("SKIP: Stony Field has no stone %d\n", world.cain.order[std::size_t(step)]); return D2D_TEST_SKIPPED; }
        assert(world.actor->events.empty());                          // no dip before the fifth
        world.cain_operate(int(stone - stony_field->npcs.begin()), 1000 + std::uint32_t(step) * 40);
    }
    assert(world.cain.stones_done && world.actor->events.size() == 1);
    const auto& dip = std::get<ev::AmbientDip>(world.actor->events[0]);
    assert(dip.fade_in == 40 && dip.hold == 220 && dip.back == 40 && dip.level == d2d::rules::level_ids::kStonyField);
    std::printf("OK: the Cairn Stones' dip, in 40, held 220, back 40, in Stony Field\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
