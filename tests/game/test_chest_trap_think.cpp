// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A chest trap's monster is made by FUN_00582420, gets its first think two
// frames later through FUN_00573780, and FUN_005fb5b0 fires only when
// FUN_005dd7f0 found a target within aip1 (25 subtiles for Trap-Missile).

#include <character.hpp>
#include <character_store.hpp>
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

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) {
        std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str());
        return D2D_TEST_SKIPPED;
    }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) {
        std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n");
        return D2D_TEST_SKIPPED;
    }
    const auto* moor = data->level(d2d::rules::level_ids::kBloodMoor);
    assert(moor);
    d2d::rules::Rng made_rng{ 7 };
    auto made_character = new_character(*data, 2, "Headless", false, true, made_rng);
    Character character;
    character.character_class = 2;
    character.name = "Headless";
    character.header = made_character.header;
    character.stats = made_character.stats;
    character.items = made_character.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    world.level = moor;

    constexpr float trap_x = 10.1f;
    constexpr float trap_y = 10.1f;
    constexpr std::uint32_t made_at = 1000;
    world.actor->player.x = trap_x + 5.f;                 // exactly 25 subtiles
    world.actor->player.y = trap_y;
    world.spring_trap(2, trap_x, trap_y, 8, made_at);
    assert(world.fight.missiles.empty());                 // the old port fired here
    world.spring_traps(made_at + 2 * kTickMs - 1);
    assert(world.fight.missiles.empty());
    world.spring_traps(made_at + 2 * kTickMs);
    assert(world.fight.missiles.size() == 1);
    assert(world.fight.missiles.back().aim_at);
    assert(std::abs(world.fight.missiles.back().aim_at->first - float(world.actor->player.x)) < 0.001f);

    world.fight.missiles.clear();
    world.actor->player.x = trap_x + 5.2f;                // 26 subtiles: searched, outside aip1
    world.spring_trap(2, trap_x, trap_y, 8, made_at + 1000);
    world.spring_traps(made_at + 1000 + 2 * kTickMs);
    assert(world.fight.missiles.empty());
    std::puts("OK: Trap-Missile waits two frames and fires only through aip1");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_chest_trap_think: %s\n", error.what());
    return 1;
}
