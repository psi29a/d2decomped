// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// `--umod-stats CASES`: d2d's champion / unique / minion stat mods on the cases
// tools/emu/umod_stats.py writes (game.exe's FUN_005a2120 and the mods' inits).
// Calls rules::boss_stats (components/rules/uniques.hpp) and applies it the way
// game::make_boss does (components/game/ai.cpp), on the case's fixed base (level,
// exp 1000, life 1000, defence 100, the row's resistances), so each field compares
// with game.exe's. A minion gets no mods, as d2d tags it (objgroups.cpp).
//
//   uv run python umod_stats.py 100000 1 > game.tsv
//   build/tests/test_umod_stats --umod-stats game.tsv > d2d.tsv
//   uv run python umod_stats.py --compare game.tsv d2d.tsv
//
// Fields d2d does not model print `*`: armor% (d2d folds it into def, the effective
// defence; --compare folds game.exe's the same way) and regen (no monster regeneration).
#include "montypes.hpp"
#include "rules.hpp"
#include "uniques.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) try {
    if (argc < 3 || std::strcmp(argv[1], "--umod-stats") != 0) { std::printf("SKIP: needs --umod-stats CASES\n"); return D2D_TEST_SKIPPED; }
    std::FILE* cases = std::fopen(argv[2], "r");
    if (cases == nullptr) { std::fprintf(stderr, "cannot open %s\n", argv[2]); return 1; }
    using namespace d2d::rules;
    UMods umods;
    std::vector<std::array<int, 3>> damage_rows;
    std::string line;
    char buf[4096];
    while (std::fgets(buf, sizeof buf, cases)) {
        line = buf;
        std::istringstream fields(line);
        std::string tag;
        fields >> tag;
        if (tag == "#k") { for (auto& value : umods.constants) fields >> value; continue; }
        if (tag == "#lvl") {
            int level = -1;
            if (!(fields >> level) || level < 0 || level > 999) throw std::runtime_error("bad #lvl row: " + line);   // the oracle's MonLvl rows
            if (std::size_t(level) >= damage_rows.size()) damage_rows.resize(std::size_t(level) + 1);
            fields >> damage_rows[std::size_t(level)][0] >> damage_rows[std::size_t(level)][1] >> damage_rows[std::size_t(level)][2];
            continue;
        }
        if (tag.empty() || tag[0] == '#') continue;
        int row, diff, base_level, velocity, base_id;
        std::string kind_s, mods_s;
        std::array<int, 6> res{};
        fields >> row >> diff >> kind_s >> mods_s >> base_level >> velocity >> base_id;
        for (auto& value : res) fields >> value;
        if (!fields || diff < 0 || diff > 2) throw std::runtime_error("bad case row: " + line);
        MonType type;
        type.velocity = velocity;
        type.base = base_id;
        type.diff[std::size_t(diff)].res = res;
        std::vector<int> mods;
        if (mods_s != "-") { std::istringstream parts(mods_s); std::string mod; while (std::getline(parts, mod, '.')) mods.push_back(std::stoi(mod)); }
        const Boss kind = kind_s == "c" ? Boss::champion : kind_s == "u" ? Boss::unique : kind_s == "s" ? Boss::superunique : Boss::minion;
        if (kind == Boss::minion) mods.clear();
        const auto boss = boss_stats(umods, type, kind, mods, diff);
        // make_boss (ai.cpp 2923..2958) on the fixed base.
        const int level = std::max(base_level + boss.level_add, 1);
        int armor_class = 100, exp = 1000;
        int life = 1000 * 256;                                // 256ths through both steps, as make_boss (ai.cpp)
        life = int(std::uint32_t(life) + std::uint32_t(d2d::rules::mul_div(life, boss.hp_pct, 100)));
        life = int(std::uint32_t(life) + std::uint32_t(d2d::rules::mul_div(life, boss.life_after_pct, 100)));
        life = std::max(life, 256);
        armor_class += armor_class * boss.defense_pct / 100;
        std::array<int, 6> boss_res{};
        if (boss.phys_resist >= 0) boss_res[0] = boss.phys_resist - res[0];
        exp *= boss.exp_mult;
        if (boss.double_defense) armor_class *= 2;
        if (boss.phys_resist < 0) boss_res = boss.res_add;
        else { const int set = boss_res[0]; boss_res = boss.res_add; boss_res[0] = set; }
        std::array<int, 6> element_rows{};                    // fire lo hi, ltng lo hi, cold lo hi
        int cold_len = 0;
        const auto& level_row = damage_rows[std::min<std::size_t>(std::size_t(level), damage_rows.size() - 1)];
        const int damage_pct = level_row[std::size_t(diff)];
        if (boss.elem >= 0) {
            const int low = damage_pct * boss.elem_min_pct / 100, high = std::max(damage_pct * boss.elem_max_pct / 100, low);
            const int slot = boss.elem == 0 ? 0 : boss.elem == 1 ? 2 : 4;
            element_rows[std::size_t(slot)] = low; element_rows[std::size_t(slot) + 1] = high;
            if (boss.elem == 2) cold_len = boss_elem_length(boss, level, int(damage_rows.size()) - 1);
        }
        int mana_lo = 0, mana_hi = 0;
        if (std::ranges::contains(mods, umod::manahit)) {
            const int base = kind == Boss::minion ? 16 : 28;
            mana_lo = damage_pct * umods.constants[std::size_t(base + diff)] / 100;
            mana_hi = std::max(damage_pct * umods.constants[std::size_t(base + 3 + diff)] / 100, mana_lo);
        }
        std::printf("%s\t%d\t%d\t%d\t%d\t%d\t%d", tag.c_str(), level, exp, life, boss.dmg_pct, boss.tohit_pct, boss.velocity_pct);
        for (int i = 0; i < 6; ++i) std::printf("\t%d", res[std::size_t(i)] + boss_res[std::size_t(i)]);
        std::printf("\t%d\t*\t*", armor_class);
        for (const int value : element_rows) std::printf("\t%d", value);
        std::printf("\t%d\t%d\t%d\n", cold_len, mana_lo << 8, mana_hi << 8);
    }
    std::fclose(cases);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
