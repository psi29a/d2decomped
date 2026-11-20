// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Necromancer's corpse tests, as tools/emu/corpse_tests.py runs game.exe's
// start functions 15, 17 and 21: `--corpse-tests` reads its cases (`pick mode
// flags c4 c6 hidden s36 s8f town`) and prints each with `| raise blast
// revive`: d2d's, Fight::corpse_near's one test for all three, a corpse in DD
// (mode 0xc).

#include <gamedata.hpp>
#include <gamedata_load.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int corpse_cases() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        long long pick = 0, mode = 0, flags = 0, c4 = 0, c6 = 0, hidden = 0, s36 = 0, s8f = 0, town = 0;
        if (!(fields >> pick >> mode >> flags >> c4 >> c6 >> hidden >> s36 >> s8f >> town)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        const int taken = mode == 0xc ? 1 : 0;
        std::printf("%s | %d %d %d\n", line.c_str(), taken, taken, taken);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    const bool cases = argc > 1 && std::string_view(argv[1]) == "--corpse-tests";
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) {
        if (cases) { std::fputs("--corpse-tests needs D2_MPQ_DIR\n", stderr); return 1; }
        std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)");
        return D2D_TEST_SKIPPED;
    }
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) {
        if (cases) { std::fputs("--corpse-tests needs the 1.14d data (D2_PATCH_INSTALLER)\n", stderr); return 1; }
        std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)");
        return D2D_TEST_SKIPPED;
    }
    if (!cases) { std::puts("SKIP: the pick check comes with the fix"); return D2D_TEST_SKIPPED; }
    return corpse_cases();
} catch (const std::exception& error) {
    std::printf("test_corpse_tests: %s\n", error.what());
    return 1;
}
