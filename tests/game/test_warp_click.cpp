// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A click takes a warp inside LvlWarp's Select box (FUN_00466870 case 5 ->
// FUN_00463ee0): the click's world pixel against the tile unit's subtile in
// world pixels ((x - y) * 16, (x + y) * 8, FUN_00643260) plus SelectX /
// SelectY, SelectDX / SelectDY on, edges in. The data part SKIPs without it.

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() {
    // The unit on subtile (50, 30): world pixel (320, 640); a box 10..30 x -20..-10 from it.
    Level::Warp warp{ .x = 10, .y = 6, .destination = 8, .exit_x = 0, .exit_y = 0, .unit_x = 50.5f / 5, .unit_y = 30.5f / 5, .select = { 10, -20, 20, 10 } };
    const auto click = [&](float pixel_x, float pixel_y) {   // a world pixel back to cells
        return World::warp_clicked(warp, (pixel_x / 80 + pixel_y / 40) / 2, (pixel_y / 40 - pixel_x / 80) / 2);
    };
    assert(click(330, 620) && click(350, 630) && click(340, 625));   // corners in
    assert(!click(329, 625) && !click(351, 625) && !click(340, 619) && !click(340, 631));
    assert(!click(320, 640));                                          // the unit itself: outside this box

    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::printf("SKIP: the data part needs 1.14d's (D2_PATCH_INSTALLER)\n"); return 0; }
    const auto* moor = data->level(d2d::rules::level_ids::kBloodMoor);
    assert(moor && !moor->warps.empty());
    for (const auto& cave : moor->warps) {
        std::printf("warp to %d: select %d %d %d %d\n", cave.destination, cave.select[0], cave.select[1], cave.select[2], cave.select[3]);
        assert(cave.select[2] > 0 && cave.select[3] > 0);
        assert(World::warp_clicked(cave, cave.unit_x, cave.unit_y));   // Blood Moor's boxes hold their unit
    }
    std::printf("OK: a warp's click box is LvlWarp's Select box from its tile unit\n");
    return 0;
}
