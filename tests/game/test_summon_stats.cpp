// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A summon's stats, as tools/emu/summon_stats.py runs game.exe's
// FUN_005c4470: `--summon-stats` reads its cases (`skill lvl clvl life n
// (skill level)*n`) and prints each with `| life b: .. a: .. s: ..`: the
// pet's max life in 256ths (rules::summon_life on the case's rolled life),
// the passive stats and the aurastats rules::summon_stats gives (`stat=value`,
// by stat id, the non-zero ones) and the skills rules::summon_skill_levels
// gives (`skill=level`), each through the owner's calcs at the case's levels.
// Needs the game data. Without it, summon_life on a few typed-in numbers.

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <skills.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int summon_stats_cases(const GameData& game_data) {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int skill = 0, lvl = 0, clvl = 0, life = 0, count = 0;
        if (!(fields >> skill >> lvl >> clvl >> life >> count)) { std::fprintf(stderr, "malformed case: %s\n", line.c_str()); return 1; }
        std::map<int, int> levels;
        for (int k = 0; k < count; ++k) {
            int other = 0, other_level = 0;
            if (!(fields >> other >> other_level)) { std::fprintf(stderr, "malformed case: %s\n", line.c_str()); return 1; }
            levels[other] = other_level;
        }
        levels[skill] = lvl;
        const auto* row = game_data.skills.get(skill);
        if (!row) { std::fprintf(stderr, "no skill %d\n", skill); return 1; }
        d2d::rules::CalcEnv env;
        const auto level_of = [&](int id) { const auto found = levels.find(id); return found == levels.end() ? 0 : found->second; };
        env.level = level_of;
        env.base_level = level_of;
        env.stat = [&](int stat) { return stat == 0xc ? clvl : 0; };
        env.clvl = clvl;
        const auto stats = d2d::rules::summon_stats(game_data.skills, *row, env, lvl);
        const auto maxhp = [&](const std::map<int, int>& part) { const auto found = part.find(7); return found == part.end() ? 0 : found->second; };
        const int max_life = d2d::rules::summon_life(life, maxhp(stats.passive) + maxhp(stats.aura), d2d::rules::eval_calc(game_data.skills, row->calc[0], env, skill, lvl));
        std::string out = std::to_string(std::int64_t(max_life) * 256) + " b:";
        for (const auto& [stat, value] : stats.passive) if (value) out += " " + std::to_string(stat) + "=" + std::to_string(value);
        out += " a:";
        for (const auto& [stat, value] : stats.aura) if (value) out += " " + std::to_string(stat) + "=" + std::to_string(value);
        out += " s:";
        for (const auto& [given, given_level] : d2d::rules::summon_skill_levels(game_data.skills, *row, env, lvl)) out += " " + std::to_string(given) + "=" + std::to_string(given_level);
        std::printf("%s | %s\n", line.c_str(), out.c_str());
    }
    return 0;
}

int life_checks() {
    assert(d2d::rules::summon_life(21, 0, 0) == 21);
    assert(d2d::rules::summon_life(0, 0, 0) == 1);                    // at least 1
    std::puts("OK: a summon's life");
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--summon-stats") {
        const char* mpq_dir = std::getenv("D2_MPQ_DIR");
        if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::fprintf(stderr, "--summon-stats needs D2_MPQ_DIR\n"); return 1; }
        const char* patch = std::getenv("D2_PATCH_INSTALLER");
        auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
        if (!data || !data->patched) { std::fprintf(stderr, "--summon-stats needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return 1; }
        return summon_stats_cases(*data);
    }
    return life_checks();
} catch (const std::exception& error) {
    std::printf("test_summon_stats: %s\n", error.what());
    return 1;
}
