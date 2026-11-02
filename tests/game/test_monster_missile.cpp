// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// `--monster-missile CASES`: what a monster's MissA / MissS1 / MissC shot
// carries, the cases of tools/emu/mm_diff.py (game.exe's FUN_0059fa30 ->
// FUN_0059f900 -> FUN_0064b860 on a real shooter): the REAL Ai::tick
// functions monster_shot_phys (256ths) and monster_shot_stats (the elements
// the missile's src carries), one line each, to diff.
//
// A case line: ID ROW FIRST CAST S1 LVL A1MIN A1MAX A2MIN A2MAX S1MIN S1MAX
// Output: ID PHYS_LO PHYS_HI then per element of mode A2: ETYPE MIN MAX DUR.

#include <ai.hpp>
#include <combat.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <monsters.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

using namespace d2d::game;

int main(int argc, char** argv) try {
    if (argc < 3 || std::string_view(argv[1]) != "--monster-missile") {
        std::printf("SKIP: an oracle mode, run `test_monster_missile --monster-missile CASES` (see tools/emu/mm_diff.py)\n");
        return D2D_TEST_SKIPPED;
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->patched);

    std::ifstream cases(argv[2]);
    std::string line;
    while (std::getline(cases, line)) {
        std::istringstream fields(line);
        int case_id = 0, first = 0, cast = 0, s1 = 0, lvl = 0;
        std::string row;
        d2d::rules::MonStats owner;
        fields >> case_id >> row >> first >> cast >> s1 >> lvl >> owner.a1_min >> owner.a1_max >> owner.a2_min >> owner.a2_max >> owner.s1_min >> owner.s1_max;
        const auto found = data->missiles.find(row);
        if (found == data->missiles.end()) { std::fprintf(stderr, "no row %s\n", row.c_str()); return 1; }
        const auto& info = found->second;
        const auto phys = monster_shot_phys(info, first ? owner.a1_min : s1 ? owner.s1_min : owner.a2_min, first ? owner.a1_max : s1 ? owner.s1_max : owner.a2_max, cast != 0, lvl);
        const auto stats = monster_shot_stats(info, owner, first != 0, cast != 0, s1 != 0, lvl);
        std::printf("%d %d %d", case_id, phys.lo, phys.hi);
        for (const auto& element : stats.elements)
            if (element.type >= 0 && element.mode == "A2") std::printf(" %d %d %d %d", element.type, element.min, element.max, element.dur);
        std::printf("\n");
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_monster_missile: %s\n", error.what());
    return 1;
}
