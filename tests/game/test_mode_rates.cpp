// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's animation rate in each mode, as tools/emu/mode_rates.py runs
// game.exe's FUN_00623f50 from its entry: `--mode-rates` reads its cases
// (`class mode skill speed vel frw fhr fbr held ias rate69`) and prints each
// with `| rate`: d2d's, the view's AnimData speed x (100 + anim_rate_pct) /
// 100 (a get-hit at rules::get_hit_rate), which reads neither the walk % nor
// the MonStats walk / run base.

#include <ai.hpp>
#include <combat.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>

#include <algorithm>
#include <array>
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

constexpr std::array<std::string_view, 16> kModes{ "DT", "NU", "WL", "GH", "A1", "A2", "BL", "SC", "S1", "S2", "S3", "S4", "DD", "KB", "SQ", "RN" };

int rate_cases(const GameData& data) {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int cls = 0, mode = 0, skill = 0, speed = 0, vel = 0, frw = 0, fhr = 0, fbr = 0, held = 0, ias = 0, rate69 = 0;
        if (!(fields >> cls >> mode >> skill >> speed >> vel >> frw >> fhr >> fbr >> held >> ias >> rate69) || mode < 0 || mode >= 16) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        Monster unit;
        unit.type = cls;
        unit.skill = skill;
        unit.props.ias = ias;
        unit.props.fhr = fhr;
        unit.anim_pct = rate69 - 100;
        const auto* row = data.skills.get(skill);
        const int rate = kModes[std::size_t(mode)] == "GH" ? d2d::rules::get_hit_rate(speed, fhr)
                                                           : std::clamp(speed * (100 + anim_rate_pct(unit, kModes[std::size_t(mode)], row && row->use_attack_rate)) / 100, 0, 0x7fff);
        std::printf("%s | %d\n", line.c_str(), rate);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    const bool cases = argc > 1 && std::string_view(argv[1]) == "--mode-rates";
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) {
        if (cases) { std::fputs("--mode-rates needs D2_MPQ_DIR\n", stderr); return 1; }
        std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)");
        return D2D_TEST_SKIPPED;
    }
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) {
        if (cases) { std::fputs("--mode-rates needs the 1.14d data (D2_PATCH_INSTALLER)\n", stderr); return 1; }
        std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)");
        return D2D_TEST_SKIPPED;
    }
    if (!cases) { std::puts("SKIP: the rate check comes with the fix"); return D2D_TEST_SKIPPED; }
    return rate_cases(*data);
} catch (const std::exception& error) {
    std::printf("test_mode_rates: %s\n", error.what());
    return 1;
}
