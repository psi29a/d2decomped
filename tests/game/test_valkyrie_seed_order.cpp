// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Before Valkyrie gear, FUN_00573cb0's look has sixteen zero bounds and
// advances nothing; its life roll advances her unit seed once.

#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>

using namespace d2d::game;

int main() {
    namespace fs = std::filesystem;
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }

    d2d::rules::Rng character_seed{ 7 };
    Character character;
    auto made = new_character(*data, 0, "Headless", false, true, character_seed);
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    character.stats.values[d2d::d2s::kLevel] = 40;

    auto world = std::make_unique<World>(&*data, -1, -1);
    world->enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world->level = &open;
    world->fight.enter(&open);
    world->fight.skill_level = [](int) { return 1; };
    world->fight.skill_base = world->fight.skill_level;
    const int valkyrie_skill = data->skills.by_name.at("Valkyrie");
    const int valkyrie_type = data->monsters.row("valkyrie");
    assert(valkyrie_type >= 0);
    data->mon_equip_first[std::size_t(valkyrie_type)] = -1;  // observe the seed exactly at the gear boundary
    world->actor->character.stats.values[d2d::d2s::kMana] = 500 << 8;
    world->actor->pmode = -1;
    assert(world->fight.cast_summon(valkyrie_skill, world->actor->player.x + 3, world->actor->player.y, 100000));

    auto game_seed = world->fight.spawning.game;
    d2d::rules::Rng expected{ game_seed.next() };
    const auto& type = data->monsters.types[std::size_t(valkyrie_type)];
    const auto look = d2d::rules::monster_look(nullptr, type.choices, expected);
    assert(std::ranges::count_if(type.choices, [](std::uint8_t count) { return count > 0; }) == 0);
    (void)look;
    expected.next();  // FUN_00573cb0's inclusive life-range draw

    world->fight.summon(*data->skills.get(valkyrie_skill), 100000);
    assert(world->actor->pets.size() == 1);
    const auto& actual = world->actor->pets.front().monster.seed;
    assert(actual.low == expected.low && actual.high == expected.high);
    std::puts("OK: Valkyrie gear starts after her life draw; her zero-bound look draws nothing");
}
