// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Smite's damage range: player_blow with swing.smite on a shield's row (FUN_005ce9f0 calls FUN_0057b420 with the shield's damage).
//   `--smite-damage`: a grid of shield rows, Holy Shield terms, enhanced damage, stat 111 (normal damage), damage percent, Str, Dex and
//   gear percents, one line each
//   `S seed_low seed_high shield_min shield_max holy_min holy_max enhanced n111 dmg_pct str dex str_bonus dex_bonus gear_pct | hit phys_fine`: the inputs (the seed's two words, the shield's min and max, Holy Shield's
//   min and max in 256ths, the skill's ED, stat 111, stat 25, Str, Dex, the shield's Str and Dex bonus, stats 17 and 18) and what d2d rolled
//   (the physical after resistance, in 256ths), as `tools/emu/smite_damage.py compare` reads them for game.exe.
//   no argument: the rules behind it.
#include <combat.hpp>
#include <d2s_items.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>

using namespace d2d::rules;

namespace {

struct Cell {
    int hit = 0, phys_fine = 0;
};

Cell blow_of(std::uint32_t seed_low, std::uint32_t seed_high, int shield_min, int shield_max, int holy_min, int holy_max, int enhanced, int n111, int dmg_pct, int str, int dex, int str_bonus, int dex_bonus, int gear_pct) {
    namespace s = d2d::d2s;
    Tables tables;
    d2d::d2s::Item shield;
    shield.code = "tsh";
    tables.item_base["tsh"] = { .mindam = shield_min, .maxdam = shield_max, .str_bonus = str_bonus, .dex_bonus = dex_bonus };
    StatSum sum{}, wsum{};
    s::Stats stats;
    stats.values[s::kLevel] = 1; stats.values[s::kStr] = str; stats.values[s::kDex] = dex;
    sum[s::kMaxDamagePercent] = sum[s::kMinDamagePercent] = gear_pct;
    sum[s::kNormalDamage] = n111; sum[s::kDamagePercent] = dmg_pct;
    const auto fighter = make_fighter(tables, nullptr, &shield, sum, wsum, stats, {}, 0, {});
    Target target;
    target.armor_class = 0; target.level = 1;
    target.hit_points = target.max_hp = 1 << 30;
    Swing swing;
    swing.smite = true; swing.ed_pct = enhanced; swing.skill_lo = holy_min; swing.skill_hi = holy_max;
    Rng rng;
    rng.low = seed_low; rng.high = seed_high;
    const auto blow = player_blow(fighter, target, 1, rng, swing);
    return { blow.hit ? 1 : 0, blow.phys_fine };
}

}   // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::strcmp(argv[1], "--smite-damage") == 0) {
        const int rows[][2] = { { 1, 2 }, { 5, 5 }, { 46, 46 }, { 9, 30 }, { 12, 12 }, { 1, 1 }, { 20, 60 } };      // the shield's min and max (armor.txt); 46 46 is the Zakarum Shield's
        const int holy_rows[][2] = { { 0, 0 }, { 256 * 3, 256 * 9 } };                                                   // Holy Shield's min and max, 256ths
        const int eds[] = { 0, 15, 45, 150, 300 };
        const int n111s[] = { -30, 0, 1, 2, 5 };
        const int dps[] = { 0, 20, 100 };
        const int stats[][2] = { { 30, 30 }, { 100, 80 } };
        const int bonuses[][2] = { { 110, 0 }, { 0, 100 }, { 75, 75 }, { 0, 0 } };
        const int offs[] = { 0, 15, 40 };
        std::uint32_t counter = 1;
        for (const auto& row : rows)
            for (const auto& holy : holy_rows)
                for (const int enhanced : eds)
                    for (const int n111 : n111s)
                        for (const int dmg_pct : dps)
                            for (const auto& st : stats)
                                for (const auto& bonus : bonuses)
                                    for (const int gear_pct : offs)
                                        for (int seed = 0; seed < 3; ++seed, ++counter) {
                                            const std::uint32_t seed_low = counter * 2654435761u + 12345u, seed_high = counter * 40503u + 666u + std::uint32_t(seed);
                                            const auto cell = blow_of(seed_low, seed_high, row[0], row[1], holy[0], holy[1], enhanced, n111, dmg_pct, st[0], st[1], bonus[0], bonus[1], gear_pct);
                                            std::printf("S %u %u %d %d %d %d %d %d %d %d %d %d %d %d | %d %d\n", seed_low, seed_high, row[0], row[1], holy[0], holy[1], enhanced, n111, dmg_pct, st[0], st[1], bonus[0], bonus[1], gear_pct, cell.hit, cell.phys_fine);
                                        }
        return 0;
    }
    std::printf("test_smite_damage: no rules to check yet\n");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "test_smite_damage: %s\n", e.what());
    return 1;
}
