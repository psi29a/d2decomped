// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_00667970: a preset's cells all take 0x10 << n for each Vis slot n with
// a level and Warp -1, so every room FUN_00667ed0 makes of an outdoor preset
// (FUN_006750f0) holds its level's walk-across slots in room2 +0x28: the
// Blood Moor's 0x30 (Vis0 the camp, Vis1 the Cold Plains), the Cold
// Plains' 0x70 (the Blood Moor, the Stony Field, the Burial Grounds). A
// plain room (FUN_0067d540) takes only its cell's +0x18 value, a contact
// span's slot on the level's edge. FUN_0066c370 walks those flags for the
// near list (tools/emu/room_vis.py, near_rooms.py).

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

// Each preset room of `level` holds `walks`; returns how many it checked.
int check_presets(const Level& level, std::uint32_t walks) {
    assert(level.room_flags.size() == level.rooms.size());
    int presets = 0;
    for (std::size_t index = 0; index < level.rooms.size(); ++index) {
        if (level.rooms[index].kind != 2) continue;
        if ((level.room_flags[index] & walks) != walks)
            std::printf("level %d room %d,%d: flags %x lack %x\n", level.id, level.rooms[index].x, level.rooms[index].y, level.room_flags[index], walks);
        assert((level.room_flags[index] & walks) == walks);
        ++presets;
    }
    return presets;
}

}  // namespace

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    const auto* moor = data->level(d2d::rules::level_ids::kBloodMoor);
    const auto* plains = data->level(d2d::rules::level_ids::kColdPlains);
    if (!moor || !plains) { std::printf("SKIP: the Blood Moor or the Cold Plains not built\n"); return D2D_TEST_SKIPPED; }
    const int moor_presets = check_presets(*moor, 0x30);
    const int plains_presets = check_presets(*plains, 0x70);
    assert(moor_presets > 0 && plains_presets > 0);
    // A plain room keeps no walk slot of its own unless a contact span crosses its cell.
    int plain_without = 0;
    for (std::size_t index = 0; index < moor->rooms.size(); ++index)
        if (moor->rooms[index].kind == 1 && !(moor->room_flags[index] & 0xff0)) ++plain_without;
    assert(plain_without > 0);
    std::printf("OK: %d Blood Moor and %d Cold Plains preset rooms hold their walk slots; %d plain rooms none\n", moor_presets, plains_presets, plain_without);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
