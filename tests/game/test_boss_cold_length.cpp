// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A Cold Enchanted unique's cold rides its blow for the unit's level x 5 + 100 frames
// (FUN_005a1c70, stat 0x38), through the REAL make_boss: the rule is rules::boss_elem_length.
// Fire and cold together both ride it; a minion takes them too, without the mods' events.
// (components/rules/uniques.hpp), the level is the unit's after make_boss's level mods.
// tools/emu/umod_stats.py runs game.exe's init against rules::boss_stats (test_umod_stats).

#include <ai.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: the oracle is 1.14d: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    for (const char* id : { "fallen1", "zombie1", "skeleton1" }) {
        const auto found = data->monsters.by_id.find(id);
        assert(found != data->monsters.by_id.end());
        for (int difficulty = 0; difficulty < 3; ++difficulty) {
            for (const int mod : { d2d::rules::umod::cold, d2d::rules::umod::fire }) {
                d2d::rules::Rng look_rng{ 1 }, rng{ 7 };
                Monster monster = make_monster(*data, int(found->second), 0, 0, look_rng, difficulty, false, 1, -1);
                make_boss(*data, monster, d2d::rules::Boss::unique, std::vector<int>{ mod }, -1, 0, difficulty, rng);
                const std::size_t type = mod == d2d::rules::umod::cold ? 2 : 0;
                assert(monster.stats.enchants[type].has_value());
                assert(monster.stats.enchant_len == (mod == d2d::rules::umod::cold ? 100 + 5 * std::clamp(monster.stats.level, 1, int(data->monsters.lvl.size()) - 1) : 0));
            }
            // Each enchant sets its own stats (FUN_005a1990 / FUN_005a1c70), so a fire and cold one carries both;
            // its minion takes both inits (FUN_0058f380, flag 0) but none of the mods' bytes.
            d2d::rules::Rng look_rng{ 1 }, rng{ 7 };
            const std::vector<int> both{ d2d::rules::umod::fire, d2d::rules::umod::cold };
            Monster leader = make_monster(*data, int(found->second), 0, 0, look_rng, difficulty, false, 1, -1);
            make_boss(*data, leader, d2d::rules::Boss::unique, both, -1, 0, difficulty, rng);
            assert(leader.stats.enchants[0] && leader.stats.enchants[2] && !leader.stats.enchants[1] && leader.mods == both);
            Monster minion = make_monster(*data, int(found->second), 0, 0, look_rng, difficulty, false, 1, -1);
            make_boss(*data, minion, d2d::rules::Boss::minion, both, -1, 0, difficulty, rng);
            assert(minion.stats.enchants[0] && minion.stats.enchants[2] && minion.mods.empty());
        }
    }
    std::printf("test_boss_cold_length: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
