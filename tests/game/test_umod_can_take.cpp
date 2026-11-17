// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// May a MonUMod row be rolled on a monster: rules::unique_detail::can_take against FUN_005a03e0
// (the check FUN_005a0500 / FUN_005a0600 make on each weighted row). Needs the MPQs (D2_MPQ_DIR, as
// test_game); it skips without.
// `--umod-can-take` prints, for every MonStats row and every MonUMod row, `monster mod allowed`: the lines of
// tools/emu/umod_can_take.py, to diff against game.exe's.

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <montypes.hpp>
#include <uniques.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string_view>

namespace fs = std::filesystem;

int main(int argc, char** argv) try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = d2d::game::load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);

    if (argc > 1 && std::string_view(argv[1]) == "--umod-can-take") {
        const auto& types = data->monsters.types;
        // The game's table is every row of MonUMod.txt in order (row 0 is the header's blank); d2d keeps the rows
        // of its file after the "Expansion" divider out, the others in order with their `id` column.
        for (std::size_t monster = 0; monster < types.size(); ++monster)
            for (const auto& mod : data->umods.rows)
                std::printf("%zu %d %d\n", monster, mod.id, int(d2d::rules::unique_detail::can_take(mod, types[monster])));
        return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--monster-ids") {
        for (std::size_t monster = 0; monster < data->monsters.types.size(); ++monster) std::printf("%zu %s\n", monster, data->monsters.types[monster].id.c_str());
        return 0;
    }
    // A monster that must not multishoot (nomultishot: the vampires, two slingers) refuses the Multiple Shots mod
    // (fPick 2, FUN_005a03e0 0x5a046e..0x5a04c5 reads MonStats +0xe bit 1) although it isn't melee; a ranged monster
    // without the flag takes it. game.exe's counts (tools/emu/umod_can_take.py): vampire1 and slinger5 take 40 of the
    // 43 rows (not 0, 24, 29), sk_archer1 41 (not 0, 24).
    const auto type_of = [&](const char* name) -> const d2d::rules::MonType& {
        const auto row = data->monsters.row(name);
        assert(row >= 0);
        return data->monsters.types[std::size_t(row)];
    };
    const auto taken = [&](const d2d::rules::MonType& type) {
        int count = 0;
        for (const auto& mod : data->umods.rows) count += int(d2d::rules::unique_detail::can_take(mod, type));
        return count;
    };
    const auto& vampire = type_of("vampire1");
    const auto& slinger = type_of("slinger5");
    const auto& archer = type_of("sk_archer1");
    assert(!vampire.melee && !slinger.melee && !archer.melee);
    int multishot_rows = 0;
    for (const auto& mod : data->umods.rows) {
        if (mod.fpick != 2) continue;
        ++multishot_rows;
        assert(!d2d::rules::unique_detail::can_take(mod, vampire) && !d2d::rules::unique_detail::can_take(mod, slinger));
        assert(d2d::rules::unique_detail::can_take(mod, archer));
    }
    assert(multishot_rows == 1 && data->umods.rows.size() == 43);
    assert(taken(vampire) == 40 && taken(slinger) == 40 && taken(archer) == 41);
    std::printf("OK: umod can_take\n");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
}
