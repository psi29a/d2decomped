// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Valkyrie's MonEquip gear through the real Fight::cast_summon and its
// action frame's Fight::summon (FUN_005c4470 -> FUN_005d6b60): at skill
// level 1 her spear and full plate (the rows of level 0), at level 30 every
// row. Her A1 is then the two-handed weapon's damage (FUN_0057b420 reads
// stats 23 / 24) and her defence takes the armour's. Against the same
// summon with her MonEquip rows taken away.
#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
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

// An Amazon on an open Blood Moor; the Valkyrie summoned at `lvl`.
d2d::rules::MonStats summon(const GameData& data, int lvl) {
    d2d::rules::Rng rng{ 7 };
    Character character;
    auto made = new_character(data, 0, "Headless", false, true, rng);
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
    const int valkyrie = data.skills.by_name.at("Valkyrie");
    auto* actor = world->actor;
    actor->character.stats.values[d2d::d2s::kMana] = 500 << 8;
    actor->pmode = -1;
    const bool cast = world->fight.cast_summon(valkyrie, actor->player.x + 3, actor->player.y, 100000);
    if (cast) world->fight.summon(*data.skills.get(valkyrie), 100000);   // its action frame's (strike.cpp)
    assert(cast && actor->pets.size() == 1);
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
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    const int type = data->monsters.row("valkyrie");
    assert(type >= 0 && data->mon_equip_first[std::size_t(type)] >= 0);
    const auto& spear = data->rules.item_base.at("spr");
    const auto& plate = data->rules.item_base.at("ful");
    for (const int lvl : { 1, 30 }) {
        const auto geared = summon(*data, lvl);
        const int first = data->mon_equip_first[std::size_t(type)];
        data->mon_equip_first[std::size_t(type)] = -1;
        const auto bare = summon(*data, lvl);
        data->mon_equip_first[std::size_t(type)] = first;
        assert(geared.armor_class >= bare.armor_class + plate.minac);
        assert(geared.a1_min != bare.a1_min || geared.a1_max != bare.a1_max);
        if (lvl == 1) assert(geared.a1_min >= spear.mindam && geared.a1_max >= spear.maxdam && geared.a1_max < bare.a1_max * 4);
        else assert(geared.a1_max > 4 * spear.maxdam);   // a higher row's weapon, not level 1's spear
        std::printf("level %d: A1 %d-%d (bare %d-%d), defence %d (bare %d)\n", lvl, geared.a1_min, geared.a1_max, bare.a1_min, bare.a1_max, geared.armor_class, bare.armor_class);
    }
    std::printf("OK: the Valkyrie wears her MonEquip gear\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
