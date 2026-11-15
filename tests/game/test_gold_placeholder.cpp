// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The gold placeholder (object 269, InitFn 28 FUN_0054f8c0): mode 2 (ON)
// first, before its piles (FUN_00559300, not dropped yet). Needs the data.

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    const auto* tower = data->level(d2d::rules::level_ids::kTowerCellarLevel5);
    if (!tower) { std::printf("SKIP: no Tower Cellar Level 5\n"); return D2D_TEST_SKIPPED; }
    if (!data->builder) { std::printf("SKIP: no level builder\n"); return D2D_TEST_SKIPPED; }
    Level one = *tower;
    one.npcs.clear();
    auto object_seed = d2d::rules::Rng(4242);
    add_object(*data, data->builder->objects, data->builder->obj_row, one, 269, 10, 10, object_seed);
    assert(one.npcs.size() == 1);
    assert(one.npcs[0].mode == "ON");                    // FUN_00624690(unit, 2)
    std::printf("OK: the gold placeholder starts ON\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_gold_placeholder: %s\n", error.what());
    return 1;
}
