// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A succubus witch's Blood Mana (skill 310, srvdofunc 30) on the player puts
// it in state 0x72: a skill's cost is asked of life (FUN_00647540: stat 6)
// and paid from it (FUN_0056bfe0 -> FUN_005d2b60); short of it, the state
// goes and life is set to one point; paid, the state goes once life is under
// the curse's Param5 (40) points. Needs the game data.

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
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Witched", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Witched";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    auto& fight = world.fight;
    auto& actor = *world.actor;
    const int fire_bolt = data->skills.by_name.at("Fire Bolt");
    const auto& bolt = *data->skills.get(fire_bolt);
    fight.skill_level = [&](int id) { return id == fire_bolt ? 1 : 0; };
    const int blood_mana = data->skills.by_name.at("Blood Mana");
    assert(data->skills.get(blood_mana)->auratarget == "blood_mana" && data->skills.get(blood_mana)->par[4] == 40);
    auto& life = actor.character.stats.values[d2d::d2s::kLife];
    auto& mana = actor.character.stats.values[d2d::d2s::kMana];
    const int cost = d2d::rules::mana_cost(bolt, 1);

    mana = 0;
    life = std::int64_t(100) << 8;
    assert(fight.skill_status(fire_bolt, 0) == 1);                     // no curse: mana asked
    actor.curses[0] = { blood_mana, 1, 1'000'000 };
    assert(fight.blood_mana() && fight.skill_status(fire_bolt, 0) == 0);   // cursed: life asked
    fight.pay(bolt, 1);
    assert(life == (std::int64_t(100) << 8) - cost && mana == 0 && fight.blood_mana());
    std::printf("OK: in state 0x72 a cast is asked of life and paid from it\n");

    life = (std::int64_t(40) << 8) + cost - 1;                           // paid, under 40 points: the state goes
    fight.pay(bolt, 1);
    assert(life == (std::int64_t(40) << 8) - 1 && !fight.blood_mana());
    actor.curses[0] = { blood_mana, 1, 1'000'000 };
    life = cost - 1;                                                     // short: life at one point, the state gone
    assert(!fight.blood_pay(cost) && life == 0x100 && !fight.blood_mana());
    std::printf("OK: Blood Mana ends under 40 points of life, or when its cost leaves one point\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_blood_mana: %s\n", error.what());
    return 1;
}
