// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A level draws with its Levels.txt Pal's act palette (Levels.bin +2), not its
// act's (+3): on a level change the client compares the two levels' Pal
// (0x465622) and loads palette\act{Pal+1} (FUN_004fb480). Abaddon, the Pit of
// Acheron and the Infernal Pit (125-127) are Act 5's with Act 4's palette.
// The rule needs no game data; the levels' own Pal is checked with it.
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    Level abaddon;
    abaddon.id = 125;
    assert(abaddon.palette_act() == 4);                         // no Pal read: its act's (Act 5)
    abaddon.pal = 3;
    assert(abaddon.palette_act() == 3);                         // Act 4's
    std::puts("OK: a level's palette is its Pal's, else its act's");

    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    const auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    const auto palette = [&](int id) { const auto* level = data->level(id); assert(level); return level->palette_act(); };
    if (!data->level(125)) { std::puts("SKIP: no Act 5 levels (classic data)"); return 0; }
    assert(palette(125) == 3 && palette(126) == 3 && palette(127) == 3);   // Abaddon, the Pit of Acheron, the Infernal Pit: Act 4's
    assert(palette(111) == 4 && palette(107) == 3);                       // the Frigid Highlands, the River of Flame: their own
    std::puts("OK: Abaddon, the Pit of Acheron and the Infernal Pit draw with Act 4's palette");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_level_palette: %s\n", error.what());
    return 1;
}
