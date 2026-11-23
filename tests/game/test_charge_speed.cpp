// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Charge's run (FUN_005cf6b0, 0x5cf833..0x5cf864): RunVelocity x
// (max(stat 0x43, 50) + Param1) / 100, including FUN_00620e80's integer
// run bonus and chill's -50.

#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <rules.hpp>
#include <skills.hpp>
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
    const auto speed_at = [&](int bonus, bool chilled) {
        hero.player_combat.velocity_pct = bonus;
        hero.chilled = chilled ? 3000 * kTickMs : 0;
        world.fight.start_move(*charge, 2000 * kTickMs);
        return hero.smove.speed;
    };
    const auto expected = [&](int bonus, bool chilled) {
        const int velocity = d2d::rules::charge_velocity(data->walk_velocity[kPaladin], data->run_velocity[kPaladin], bonus, chilled, charge->par[0]);
        return cells_per_sec(float(velocity) / 256.f);
    };
    for (const int bonus : { -200, -80, 0, 40 })
        for (const bool chilled : { false, true })
            assert(std::abs(speed_at(bonus, chilled) - expected(bonus, chilled)) < 1e-6f);
    std::printf("OK: Charge includes its integer run bonus and chill, at least 50\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
