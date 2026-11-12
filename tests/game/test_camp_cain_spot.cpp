// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Camp Cain (cain5, hcIdx 265) stands by the camp's "cain start position",
// object 385 (DS1 preset 110), on FUN_00545340's size-2 spot (FUN_00592960
// at 0x5929fe, from FUN_005940e0's +0x84 / +0x88), in each of the four camps.

#include <drlg.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <monster_ids.hpp>
#include <obj_preset.hpp>
#include <object_ids.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
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
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 1);
    assert(data);
    if (!data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

    static_assert(kObjPreset[0][110] == d2d::rules::object_ids::kCainStart);   // 0x748ad8's act 1 row
    std::array<bool, 4> seen{};
    for (std::uint32_t seed = 1; seed < 200 && !std::ranges::all_of(seen, [](bool was) { return was; }); ++seed) {
        set_map_seed(*data, seed, 0);
        const int camp = d2d::drlg::town_file(data->act1_layout);
        assert(camp >= 0 && camp < 4);
        if (seen[std::size_t(camp)]) continue;
        seen[std::size_t(camp)] = true;
        const auto& town = data->town;
        const auto& objects = town.ds1.objects();
        const auto start = std::ranges::find_if(objects, [](const auto& object) {
            return object.type == d2d::rules::unit_type::kObject && object.id == 110;
        });
        assert(start != objects.end());
        const auto cain = std::ranges::find(town.npcs, d2d::rules::monster_ids::kCampCain, &Npc::hc_idx);
        assert(cain != town.npcs.end());
        const auto spot = town.free_area(start->x, start->y, 2, 0x100, 100);
        const int want_x = spot ? spot->first : start->x, want_y = spot ? spot->second : start->y;
        assert(int(std::floor(cain->x * 5)) == want_x && int(std::floor(cain->y * 5)) == want_y);
        std::printf("camp %d: object 385 at (%d, %d), Cain at (%d, %d)\n", camp, start->x, start->y, want_x, want_y);
    }
    assert(std::ranges::all_of(seen, [](bool was) { return was; }));
    std::printf("OK: camp Cain stands by object 385 in all four camps\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
