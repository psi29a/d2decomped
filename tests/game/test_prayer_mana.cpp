// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A player's Prayer (do 65, FUN_005cf010) costs its FUN_00644b10 mana,
// (16 + 3 x (lvl - 1)) << 4 in 256ths: short of it, the pulse heals no one;
// a pulse that heals takes it off (FUN_0056c110); one that heals no one (at
// full life) takes nothing. The merc's Prayer costs nothing. Needs the
// game data.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <world.hpp>

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
    if (!data) { std::printf("FAIL: load_game_data failed\n"); return 1; }
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng(7);
    constexpr int kPaladin = 3;
    auto made = new_character(*data, kPaladin, "Headless", false, true, rng);
    Character character;
    character.character_class = kPaladin;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    auto& fight = world.fight;
    auto& actor = *world.actor;
    const int prayer = data->skills.by_name.at("Prayer");
    const auto& skill = *data->skills.get(prayer);
    const int level = 3;
    fight.skill_level = [&](int id) { return id == prayer ? level : 0; };
    actor.aura = prayer;
    auto& life = actor.character.stats.values[d2d::d2s::kLife];
    auto& mana = actor.character.stats.values[d2d::d2s::kMana];
    const auto max_life = actor.character.stats.values[d2d::d2s::kMaxLife];
    const int cost = d2d::rules::mana_formula(skill, level);
    assert(cost == (16 + 3 * 2) << 4);                            // Skills.txt: mana 16, lvlmana 3, manashift 4
    std::uint32_t now = 2000;
    auto pulse = [&] { actor.aura_next = 0; fight.aura_pulse(now); now += 1000; };

    life = 256; mana = 1000;
    pulse();
    std::printf("level %d, cost %d: life 256 -> %lld, mana 1000 -> %lld\n", level, cost, static_cast<long long>(life), static_cast<long long>(mana));
    assert(life > 256 && mana == 1000 - cost);

    life = max_life; mana = 1000;                                 // no one to heal: no cost
    pulse();
    assert(life == max_life && mana == 1000);

    life = 256; mana = cost - 1;                                  // short of it: no heal
    pulse();
    assert(life == 256 && mana == cost - 1);
    mana = cost;                                                  // exactly enough
    pulse();
    assert(life > 256 && mana == 0);

    // The merc's Prayer: no player cast, no cost.
    actor.aura = -1;
    Monster merc;
    merc.hit_points = 1;
    merc.unit.x = actor.player.x + 1; merc.unit.y = actor.player.y;
    actor.merc = merc;
    actor.merc_ids = { prayer, 0, 0, 0, 0, 0 }; actor.merc_levels = { 3, 0, 0, 0, 0, 0 };
    actor.merc_aura = prayer;
    actor.merc_aura_next = 0;
    life = 256; mana = 0;
    fight.aura_pulse(now);
    assert(life > 256 && actor.merc->hit_points > 1 && mana == 0);
    std::puts("OK: a player's Prayer needs and draws its mana, the merc's doesn't");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_prayer_mana: %s\n", error.what());
    return 1;
}
