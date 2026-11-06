// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_00458dc0 (0x458dc0)'s unit type 1 branch: a monster's MonStats row
// +0x18 (MonStatsEx) names its MonStats2 row, whose automapCel (+0x118)
// is its automap icon. In 1.14d only barricadetower has one, cel 1258.
// Needs the game data.

#include <gamedata.hpp>
#include <gamedata_load.hpp>

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    const int tower = data->monsters.row("barricadetower");
    assert(tower >= 0 && data->mon_npc[std::size_t(tower)].automap == 1258);
    int with_cel = 0;
    for (const auto& npc : data->mon_npc) with_cel += npc.automap > 0;
    assert(with_cel == 1);
    std::printf("monster automap cel: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_monster_automap_cel: %s\n", error.what());
    return 1;
}
