// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// `--boss-level CASES`: a champion's, unique's, superunique's or minion's
// stats through the REAL make_boss (rules::boss_stats, rules::monster_stats),
// the cases of tools/emu/boss_level.py (game.exe's FUN_00573cb0,
// FUN_005a2120, FUN_005a4f50): `id | lvl life256 def exp ar dmg_lo dmg_hi vel`,
// one line each, to diff. An optional last field is the Levels.txt id the
// unit is made in (Nightmare / Hell start at that level's MonLvl); kind p is a
// plain monster, made by make_monster alone.

#include <ai.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <monsters.hpp>
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
        int level_id = -1;
        fields >> level_id;
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
        d2d::rules::Rng look_rng{ 1 };
        d2d::rules::Rng rng{ std::uint32_t(seed & 0xffffffffU) };   // the unit seed: the life roll is its first draw
        if (kind == "p") {
            // make_monster rolls the unit's look first, so its life is not the unit seed's first draw: take the life from
            // the same call on the seed, the rest from make_monster itself.
            const Monster plain = make_monster(*data, row, 0, 0, look_rng, difficulty, true, 1, level_id);
            const auto rolled = d2d::rules::monster_stats(data->monsters, row, difficulty, rng, 0, 1, area_monster_level(*data, level_id, difficulty));
            // Scripted zero-life rows still need to run their ordinary spawn's AI.
            if (rolled.hit_points == 0) assert(plain.alive());
            const auto& stats = plain.stats;
            std::printf("%s | %d %d %d %d %d %d %d %d\n", id.c_str(), stats.level, rolled.hit_points * 256, stats.armor_class, stats.exp, stats.to_hit,
                        stats.a1_min, stats.a1_max, 0);
            continue;
        }
        const auto boss = kind == "c" ? d2d::rules::Boss::champion : kind == "u" ? d2d::rules::Boss::unique
                        : kind == "s" ? d2d::rules::Boss::superunique : d2d::rules::Boss::minion;
        Monster monster = make_monster(*data, row, 0, 0, look_rng, difficulty, false, 1, level_id);
        make_boss(*data, monster, boss, mods, -1, 0, difficulty, rng);
        const auto& stats = monster.stats;
        assert(monster.life_fine() == monster.max_life_fine());
        std::printf("%s | %d %d %d %d %d %d %d %d\n", id.c_str(), stats.level, monster.max_life_fine(), stats.armor_class, stats.exp, stats.to_hit,
                    stats.a1_min, stats.a1_max, monster.boss_speed);
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
