// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Camp Cain made (FUN_00592960): from his start object (385) a size-2 free spot (FUN_00545340), then
// MonStats 0x109 within 5 of it on the camp room's seed (FUN_005b2f20 -> FUN_005b2a00: rings of 3
// subtiles), so he stands within 15 subtiles of that spot where he fits, and the room's seed moved.
// A character with The Search for Cain done joins (CainQuest::join: camp_due).

#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <object_ids.hpp>
#include <obj_preset.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq (D2_MPQ_DIR)\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    const auto& town = data->town;
    const auto start = std::ranges::find_if(town.ds1.objects(), [](const auto& object) {
        return object.type == d2d::rules::unit_type::kObject && object.id >= 0 && object.id < 150
            && kObjPreset[0][std::size_t(object.id)] == d2d::rules::object_ids::kCainStart;
    });
    if (start == town.ds1.objects().end()) { std::printf("SKIP: no Cain start in this camp\n"); return D2D_TEST_SKIPPED; }
    int free_x = start->x, free_y = start->y;
    if (const auto spot = town.free_area(free_x, free_y, 2, 0x100, 100)) std::tie(free_x, free_y) = *spot;

    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, 1, "Seeker", false, true, rng);
    Character character;
    character.character_class = 1;
    character.name = "Seeker";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    d2d::rules::quest::set(character.header.quests[0], d2d::rules::CainQuest::kQuest, 0);   // rescued in an earlier game
    World world(&*data, -1, -1);
    world.enter(character);
    assert(world.cain.camp_cain);
    const auto cain = std::ranges::find_if(town.npcs, [](const Npc& npc) { return npc.hc_idx == d2d::rules::monster_ids::kCampCain; });
    assert(cain != town.npcs.end());
    const int at_x = int(std::floor(cain->x * 5)), at_y = int(std::floor(cain->y * 5));
    assert(std::abs(at_x - free_x) <= 15 && std::abs(at_y - free_y) <= 15);
    assert(std::max(std::abs(at_x - free_x), std::abs(at_y - free_y)) >= 3);   // the rings start 3 out: never the free spot itself
    assert(!town.unit_blocked(cain->x, cain->y, cain->shape));
    std::printf("OK: camp Cain at (%d, %d), the free spot (%d, %d)\n", at_x, at_y, free_x, free_y);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_camp_cain_spot: %s\n", error.what());
    return 1;
}
