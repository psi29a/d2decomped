// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A Raven's damage and to-hit through the real Fight::cast_summon and its
// action frame's Fight::summon. Its MonStats row (druidhawk) has none;
// SkillDamage (+0xa8) names Raven, and the per-mode damage (FUN_005a4f50 ->
// FUN_0056e050 -> FUN_0056e000) adds Raven's MinDam..MaxDam (2-4, +1 / +1 a
// level to 8) to A1, A2 and S1 and its ToHit (100, +15 a level) to each
// mode's to-hit. A2TH / S1TH are empty, so those are MonLvl's attack rating
// at its level (stat 0x13, FUN_005c49e0 0x5c4adf) plus the skill's.
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>

using namespace d2d::game;

namespace {

// A level-40 Druid on an open Blood Moor; a Raven summoned at `lvl`, then
// its swings with the Druid at `now_lvl` in Raven (pet_skill_damage, each
// frame: FUN_0056e000 takes the owner's level at the swing).
d2d::rules::MonStats summon_raven(const GameData& data, int lvl, int now_lvl) {
    d2d::rules::Rng rng{ 7 };
    Character character;
    auto made = new_character(data, d2d::d2s::kDruid, "Headless", false, true, rng);
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(data, character.header, character.items, character.stats);
    character.stats.values[d2d::d2s::kLevel] = 40;
    auto world = std::make_unique<World>(&data, -1, -1);
    world->enter(character);
    Level open = *data.level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world->level = &open;
    world->fight.enter(&open);
    world->fight.skill_level = [lvl](int) { return lvl; };
    world->fight.skill_base = world->fight.skill_level;
    const int raven = data.skills.by_name.at("Raven");
    auto* actor = world->actor;
    actor->character.stats.values[d2d::d2s::kMana] = 500 << 8;
    actor->pmode = -1;
    const bool cast = world->fight.cast_summon(raven, actor->player.x + 3, actor->player.y, 100000);
    if (cast) world->fight.summon(*data.skills.get(raven), 100000);   // its action frame's (strike.cpp)
    assert(cast && actor->pets.size() == 1);
    world->fight.skill_level = [now_lvl](int) { return now_lvl; };
    world->fight.pet_skill_damage(actor->pets.front());
    return actor->pets.front().monster.stats;
}

}   // namespace

int main() try {
    namespace fs = std::filesystem;
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    const auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    struct Expected { int lvl, low, high, to_hit; };
    for (const auto& expected : { Expected{ 1, 2, 4, 100 }, Expected{ 5, 6, 8, 160 } })
        for (const int cast_lvl : { 1, 5 }) {   // cast at either level, it swings at the owner's level now
            const auto stats = summon_raven(*data, cast_lvl, expected.lvl);
            assert(stats.a1_min == expected.low && stats.a1_max == expected.high);
            assert(stats.a2_min == expected.low && stats.a2_max == expected.high);
            assert(stats.s1_min == expected.low && stats.s1_max == expected.high);
            const int row_to_hit = data->monsters.lvl[std::size_t(stats.level)].to_hit[0];
            assert(stats.to_hit2 == row_to_hit + expected.to_hit && stats.to_hit_s1 == row_to_hit + expected.to_hit);
            std::printf("cast at %d, now level %d: A1 %d-%d, A2 to-hit %d\n", cast_lvl, expected.lvl, stats.a1_min, stats.a1_max, stats.to_hit2);
        }
    std::printf("OK: a Raven hits with its SkillDamage skill's damage and to-hit\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
