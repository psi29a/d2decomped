// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// `--boss-level CASES`: a champion's, unique's, superunique's or minion's
// stats through the REAL make_boss (rules::boss_stats, rules::monster_stats),
// the cases of tools/emu/boss_level.py (game.exe's FUN_00573cb0,
// FUN_005a2120, FUN_005a4f50): `id | lvl life def exp ar dmg_lo dmg_hi vel`,
// one line each, to diff.

#include <ai.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <uniques.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace d2d::game;

int main(int argc, char** argv) try {
    if (argc < 3 || std::string_view(argv[1]) != "--boss-level") {
        std::printf("SKIP: an oracle mode, run `test_boss_level --boss-level CASES` (see tools/emu/boss_level.py)\n");
        return D2D_TEST_SKIPPED;
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: the oracle is 1.14d's game.exe: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    std::ifstream case_file(argv[2]);
    std::string line;
    while (std::getline(case_file, line)) {
        std::istringstream fields(line);
        std::string id, name, kind, mod_list;
        int row = 0, difficulty = 0;
        std::uint64_t seed = 0;
        if (!(fields >> id >> name >> row >> difficulty >> kind >> mod_list >> seed)) continue;
        // The MonStats row, if d2d's row of that index is the same Id (d2d keeps one of a duplicated Id).
        if (row < 0 || std::size_t(row) >= data->monsters.types.size() || data->monsters.types[std::size_t(row)].id != name) {
            std::printf("%s | no-type\n", id.c_str());
            continue;
        }
        std::vector<int> mods;
        if (mod_list != "-") {
            std::istringstream parts(mod_list);
            for (std::string part; std::getline(parts, part, '.');) mods.push_back(std::stoi(part));
        }
        const auto boss = kind == "c" ? d2d::rules::Boss::champion : kind == "u" ? d2d::rules::Boss::unique
                        : kind == "s" ? d2d::rules::Boss::superunique : d2d::rules::Boss::minion;
        d2d::rules::Rng look_rng{ 1 };
        Monster monster = make_monster(*data, row, 0, 0, look_rng, difficulty, false);
        d2d::rules::Rng rng{ std::uint32_t(seed & 0xffffffffU) };   // the unit seed: the life roll is its first draw
        make_boss(*data, monster, boss, mods, -1, 0, difficulty, rng);
        const auto& stats = monster.stats;
        std::printf("%s | %d %d %d %d %d %d %d %d\n", id.c_str(), stats.level, monster.hit_points, stats.armor_class, stats.exp, stats.to_hit,
                    stats.a1_min, stats.a1_max, monster.boss_speed);
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
