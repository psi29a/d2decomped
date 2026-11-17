// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's curse slot, as tools/emu/curse_put.py runs game.exe's
// timed-state put (FUN_0056e970): `--curse-put` reads its puts (`seq skill
// level length frame`, one monster a seq, frames in order) and prints `seq
// skill level length frame taken skill level left`. Each goes through
// put_state on the monster's curse slot (fight/states.cpp, as the curse do
// calls it), `length` frames from `frame`; then whether it was taken and the
// curse the monster holds: its skill, level and frames left (-1 -1 -1: none).
// Needs the game data.

#include <ai.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int curse_put_cases(const GameData& game_data) {
    Monster monster;
    int last = -1;
    std::string line;
    while (std::getline(std::cin, line)) {
        int seq = 0, skill = 0, level = 0, length = 0, frame = 0;
        if (line.empty()) continue;
        if (std::sscanf(line.c_str(), "%d %d %d %d %d", &seq, &skill, &level, &length, &frame) != 5) {
            std::fprintf(stderr, "malformed put: %s\n", line.c_str());
            return 1;
        }
        if (seq != last) { monster = Monster{}; last = seq; }
        const auto now = std::uint32_t(frame) * 40;
        const bool taken = put_state(game_data, monster, monster.curse, { skill, level, now + std::uint32_t(length) * 40 }, now);
        const auto& held = monster.curse;
        if (held.skill >= 0 && now < held.until)
            std::printf("%d %d %d %d %d %d %d %d %d\n", seq, skill, level, length, frame, taken ? 1 : 0, held.skill, held.level, int((held.until - now) / 40));
        else
            std::printf("%d %d %d %d %d %d -1 -1 -1\n", seq, skill, level, length, frame, taken ? 1 : 0);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--curse-put") {
        const char* mpq_dir = std::getenv("D2_MPQ_DIR");
        if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::fprintf(stderr, "--curse-put needs D2_MPQ_DIR\n"); return 1; }
        const char* patch = std::getenv("D2_PATCH_INSTALLER");
        auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
        if (!data || !data->patched) { std::fprintf(stderr, "--curse-put needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return 1; }
        return curse_put_cases(*data);
    }
    std::puts("SKIP: run with --curse-put < puts (tools/emu/curse_put.py)");
    return D2D_TEST_SKIPPED;
} catch (const std::exception& error) {
    std::printf("test_curse_put: %s\n", error.what());
    return 1;
}
