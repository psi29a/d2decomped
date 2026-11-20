// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's walk back to its leader, as tools/emu/leader_follow.py runs
// game.exe's FUN_005b0ff0: `--leader-follow` reads its cases (`id type pick
// found dying gap backed`) and prints each with `| acted id back path walk`:
// d2d's, which has no such step (nothing done, +0xc kept).

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

int follow_cases() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        long long ident = 0, kind = 0, pick = 0, found = 0, dying = 0, gap = 0, backed = 0;
        if (!(fields >> ident >> kind >> pick >> found >> dying >> gap >> backed)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        std::printf("%s | 0 %lld 0 0 0\n", line.c_str(), ident);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    const bool cases = argc > 1 && std::string_view(argv[1]) == "--leader-follow";
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) {
        if (cases) { std::fputs("--leader-follow needs D2_MPQ_DIR\n", stderr); return 1; }
        std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)");
        return D2D_TEST_SKIPPED;
    }
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) {
        if (cases) { std::fputs("--leader-follow needs the 1.14d data (D2_PATCH_INSTALLER)\n", stderr); return 1; }
        std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)");
        return D2D_TEST_SKIPPED;
    }
    if (!cases) { std::puts("SKIP: the walk check comes with the fix"); return D2D_TEST_SKIPPED; }
    return follow_cases();
} catch (const std::exception& error) {
    std::printf("test_leader_follow: %s\n", error.what());
    return 1;
}
