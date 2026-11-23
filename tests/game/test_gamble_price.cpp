// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// What the gamble tab charges for a base: rules::gamble_price against FUN_00629370 (through
// FUN_0062efb0 mode 2). Needs the MPQs (D2_MPQ_DIR, as test_game); it skips without.
// `--gamble-price CASES` prints, per case `id code clvl reduced`, `id price`: the cases of
// tools/emu/gamble_price.py, to diff against game.exe's.

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <vendor.hpp>

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
    // An upgraded base costs what its normal one does (FUN_00629370 reads the code FUN_006287d0 answers): a Hatchet
    // (9ha, exceptional) and a Tomahawk (7ha, elite) cost a Hand Axe's (hax) price. game.exe's numbers, from
    // tools/emu/gamble_price.py: clvl 1 771, clvl 30 6,896, clvl 45 12,933, and 11,640 with 10 percent reduced prices.
    for (const char* code : { "hax", "9ha", "7ha" }) {
        assert(d2d::rules::gamble_price(data->rules, code, 1) == 771);
        assert(d2d::rules::gamble_price(data->rules, code, 30) == 6896);
        assert(d2d::rules::gamble_price(data->rules, code, 45) == 12933);
        assert(d2d::rules::gamble_price(data->rules, code, 45, 10) == 11640);
    }
    std::printf("OK: gamble prices\n");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
}
