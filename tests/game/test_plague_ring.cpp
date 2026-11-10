// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Hit function 2 (FUN_005a9d80 -> FUN_005a9370) through the REAL Fight::burst: Plague Javelin (sHitPar1 1,
// sHitPar2 2, sHitPar3 3) sends plaguejavcloud the 8 even ways at its Param1 (2) << 7 and ways 1..15 at its
// Param2 (4) << 7, x 75 / 100: 23 clouds, each Range 60 + (SubStop 19 - SubStart 10) x 3 = 87 frames (create
// flag 8, FUN_0059fa30).

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng{ 7 };
    Character character;
    auto made = new_character(*data, 0, "Headless", false, true, rng);
    character.character_class = 0;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    auto world = std::make_unique<World>(&*data, -1, -1);
    world->enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    world->level = &open;
    world->fight.enter(&open);
    world->fight.skill_level = [](int) { return 1; };
    auto& fight = world->fight;
    const std::uint32_t now = 1000 * kTickMs;

    const auto clouds = [&](const char* row, const char* skill, const char* cloud) {
        Missile javelin;
        javelin.info = &data->missiles.at(row);
        javelin.skill = data->skills.by_name.at(skill);
        javelin.level = 1;
        javelin.friendly = true;
        javelin.x = javelin.y = 20.1f;
        fight.pending.clear();
        fight.burst(javelin, now);
        int laid = 0;
        for (const auto& missile : fight.pending) {
            if (!missile.info || missile.info->name != cloud) continue;
            ++laid;
            assert(missile.dies == now + std::uint32_t(87 * 40));
        }
        return laid;
    };
    assert(clouds("plaguejavelin", "Plague Javelin", "plaguejavcloud") == 23);
    const auto& even = fight.pending.front();
    assert(std::abs(std::hypot(even.velocity_x, even.velocity_y) - missile_speed((2 << 7) * 75 / 100)) < 1e-4f);
    std::printf("OK: Plague Javelin's hit sends 23 clouds of 87 frames (FUN_005a9370)\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_plague_ring: %s\n", error.what());
    return 1;
}
