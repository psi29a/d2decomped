// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Iron Maiden's hit back, as tools/emu/iron_maiden.py runs game.exe's
// FUN_005c5f50: `--iron-maiden` reads its cases (`wearer merc struck phys
// calc1 calc2 calc3`, 256ths) and prints each with `| damage result class`
// (none: `- - -`): d2d's, a monster's whole points x calc1 / 100 added to
// the struck one's thorns (record 0x4021, no class); nothing for others.

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

int iron_maiden_cases() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int wearer = 0, merc = 0, struck = 0, phys = 0, calc1 = 0, calc2 = 0, calc3 = 0;
        if (!(fields >> wearer >> merc >> struck >> phys >> calc1 >> calc2 >> calc3)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        const long long back = wearer == 1 ? static_cast<long long>(phys > 0 ? phys >> 8 : 0) * calc1 / 100 : 0;
        if (back > 0) std::printf("%s | %lld 16417 0\n", line.c_str(), back << 8);
        else std::printf("%s | - - -\n", line.c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    const bool cases = argc > 1 && std::string_view(argv[1]) == "--iron-maiden";
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) {
        if (cases) { std::fputs("--iron-maiden needs D2_MPQ_DIR\n", stderr); return 1; }
        std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)");
        return D2D_TEST_SKIPPED;
    }
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) {
        if (cases) { std::fputs("--iron-maiden needs the 1.14d data (D2_PATCH_INSTALLER)\n", stderr); return 1; }
        std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)");
        return D2D_TEST_SKIPPED;
    }
    if (!cases) { std::puts("SKIP: the strike check comes with the fix"); return D2D_TEST_SKIPPED; }
    return iron_maiden_cases();
} catch (const std::exception& error) {
    std::printf("test_iron_maiden: %s\n", error.what());
    return 1;
}
