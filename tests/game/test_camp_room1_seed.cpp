// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The camp's room seeds are its rooms' room1 seeds (FUN_006422a0: the room's tile
// seed stepped once more after its tile picks), not the DRLG room's own: keep_camp_seeds
// lays the camp's tiles (preset_room1_seeds) and keeps those. Expected values are game.exe's
// (tools/emu camp_room1.py, map seed 3, rooms up in list order). Needs the game's MPQs
// (D2_MPQ_DIR) and the 1.14d patch (D2_PATCH_INSTALLER).

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <spawning.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <thread>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 3);
    assert(data);
    if (!data->patched) { std::printf("SKIP: these seeds are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    Spawning::LevelState camp;
    bool kept = false;
    for (int tries = 0; tries < 200 && !(kept = keep_camp_seeds(*data, data->town, camp)); ++tries) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(kept);
    struct Expected { int x, y; std::uint32_t seed; };
    for (const Expected expected : { Expected{ 0, 0, 0xba3a6a69 }, { 8, 0, 0x7459545f }, { 8, 16, 0x8e270d24 }, { 48, 32, 0x55f76344 } }) {
        bool found = false;
        for (std::size_t index = 0; index < camp.room_seeds.size(); ++index) {
            const auto rect = level_room(data->town, index);
            if (!rect || rect->x != expected.x || rect->y != expected.y) continue;
            found = true;
            assert(camp.room_seeds[index].low == expected.seed);
        }
        assert(found);
    }
    std::printf("OK: the camp's room seeds are game.exe's room1 seeds\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_camp_room1_seed: %s\n", error.what());
    return 1;
}
