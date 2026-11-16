// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// What the gamble tab charges for a base: rules::gamble_price against FUN_00629370 (through
// FUN_0062efb0 mode 2). Needs the MPQs (D2_MPQ_DIR, as test_game); it skips without.
// `--gamble-price CASES` prints, per case `id code clvl reduced`, `id price`: the cases of
// tools/emu/gamble_price.py, to diff against game.exe's.

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
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

    if (argc > 2 && std::string_view(argv[1]) == "--gamble-price") {
        std::ifstream cases(argv[2]);
        if (!cases) { std::fprintf(stderr, "gamble-price: cannot open %s\n", argv[2]); return 1; }
        std::string id, code;
        int clvl = 0, reduced = 0;
        while (cases >> id >> code >> clvl >> reduced)
            std::printf("%s %d\n", id.c_str(), d2d::rules::gamble_price(data->rules, code, clvl, reduced));
        return 0;
    }

    // A ring costs its gamble cost whatever the level: 50,000 in the stock tables.
    assert(d2d::rules::gamble_price(data->rules, "rin", 30) == 50000);
    std::printf("OK: gamble prices\n");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
}
