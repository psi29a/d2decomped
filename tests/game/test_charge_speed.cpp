// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Charge's run (FUN_005cf6b0, 0x5cf833..0x5cf864): RunVelocity x
// (max(stat 0x43, 50) + Param1) / 100, the player's stat 0x43 100 + its
// velocitypercent bonuses.

#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cmath>
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

    d2d::rules::Rng rng(7);
    constexpr int kPaladin = 3;
    auto made = new_character(*data, kPaladin, "Charger", false, true, rng);
    Character character;
    character.character_class = kPaladin;
    character.name = "Charger";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    auto& hero = *world.actor;

    const auto* charge = data->skills.get(data->skills.by_name.at("Charge"));
    assert(charge);
    const auto speed_at = [&](int bonus) {
        hero.player_combat.velocity_pct = bonus;
        world.fight.start_move(*charge, 2000 * kTickMs);
        return hero.smove.speed;
    };
    const float plain = speed_at(0), quick = speed_at(40), slowed = speed_at(-80);
    const float par1 = float(charge->par[0]);
    assert(plain > 0 && std::abs(quick / plain - (140 + par1) / (100 + par1)) < 1e-4f);
    assert(std::abs(slowed / plain - (50 + par1) / (100 + par1)) < 1e-4f);   // at least 50
    std::printf("OK: Charge runs at its velocitypercent, at least 50\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
