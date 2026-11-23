// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// An object's start mode: made in mode 0 (a preset by the DS1 loader, FUN_00665950; a group's by
// FUN_00555230), then whatever its InitFn (table 0x731bc0) sets: the LargeUrn (4, InitFn 2: a trap
// roll, no mode) stays NU although its Mode2 is lit; a torch (102, InitFn 8: mode 2) is ON; the
// afterglow dummy (131, InitFn 59) OP; a waypoint (119, InitFn 17) ON in a town, NU outside.

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq (D2_MPQ_DIR)\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    if (!data->builder) { std::printf("SKIP: no level builder\n"); return D2D_TEST_SKIPPED; }
    const auto mode_in = [&](int level_id, int object) {
        const auto* level = data->level(level_id);
        assert(level);
        Level one = *level;
        one.npcs.clear();
        auto seed = d2d::rules::Rng(4242);
        add_object(*data, data->builder->objects, data->builder->obj_row, one, object, 10, 10, seed);
        assert(one.npcs.size() == 1);
        return one.npcs[0].mode;
    };
    namespace ids = d2d::rules::level_ids;
    assert(mode_in(ids::kBloodMoor, 4) == "NU");                     // LargeUrn: InitFn 2 sets no mode
    assert(mode_in(ids::kBloodMoor, 102) == "ON");                   // InitFn 8: FUN_00624690(unit, 2)
    assert(mode_in(ids::kBloodMoor, 131) == "OP");                   // InitFn 59: mode 1 from 0
    assert(mode_in(ids::kRogueEncampment, 119) == "ON" && mode_in(ids::kColdPlains, 119) == "NU");
    std::printf("OK: start modes are the InitFns'\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_object_start_mode: %s\n", error.what());
    return 1;
}
