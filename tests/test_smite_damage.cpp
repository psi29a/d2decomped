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

// game.exe's answers (tools/emu/smite_damage.py compare printed the same rolls): the seed's two words, the shield's min and max, Holy Shield's min and max
// in 256ths, ED, stat 111, stat 25, Str, Dex, the shield's Str and Dex bonus, stats 17 and 18, the roll in 256ths; the comment: what d2d rolled before.
struct Frozen { std::uint32_t seed_low, seed_high; int shield_min, shield_max, holy_min, holy_max, enhanced, n111, dmg_pct, str, dex, str_bonus, dex_bonus, gear_pct, phys_fine; };
constexpr Frozen kFrozen[] = {
    { 652624805U, 2583606031U, 1, 1, 768, 2304, 300, -30, 0, 100, 80, 0, 0, 15, 1984 },   // parent 6584
    { 922646089U, 2935010059U, 20, 60, 768, 2304, 45, -30, 20, 30, 30, 0, 0, 15, 5848 },   // parent 19141
    { 3713964479U, 2627430276U, 20, 60, 0, 0, 0, -30, 0, 100, 80, 0, 0, 40, 4397 },   // parent 21375
    { 2532506732U, 2583241504U, 1, 1, 768, 2304, 300, -30, 0, 100, 80, 75, 75, 15, 1980 },   // parent 9020
    { 1283513978U, 182142659U, 1, 2, 0, 0, 300, -30, 100, 30, 30, 0, 0, 15, 2007 },   // parent 2007
    { 1703333774U, 50750926U, 1, 2, 0, 0, 15, -30, 100, 30, 30, 0, 0, 0, 592 },   // parent 592
    { 453387706U, 962392449U, 46, 46, 0, 0, 45, -30, 0, 30, 30, 110, 0, 0, 7309 },   // parent 20961
    { 2582298277U, 2365861903U, 1, 1, 0, 0, 300, -30, 20, 30, 30, 75, 75, 0, 1675 },   // parent 1187
    { 2928366749U, 2318878424U, 1, 1, 0, 0, 150, -30, 0, 30, 30, 0, 100, 0, 1133 },   // parent 716
    { 601314231U, 962918989U, 46, 46, 0, 0, 45, -30, 0, 30, 30, 0, 100, 15, 8046 },   // parent 22374
    { 3687202493U, 2195749303U, 1, 1, 0, 0, 0, -30, 100, 100, 80, 0, 0, 15, 550 },   // parent 550
    { 1803411660U, 2276066752U, 1, 1, 0, 0, 45, -30, 0, 30, 30, 0, 0, 40, 473 },   // parent 473
    { 346752092U, 1452397745U, 9, 30, 0, 0, 150, 0, 0, 30, 30, 110, 0, 0, 8845 },   // parent 8845
    { 15047613U, 671540407U, 5, 5, 768, 2304, 0, 0, 100, 30, 30, 75, 75, 0, 7970 },   // parent 7970
    { 102138697U, 581299724U, 5, 5, 0, 0, 150, 0, 20, 30, 30, 75, 75, 15, 4578 },   // parent 4211
    { 1999269290U, 495149843U, 5, 5, 0, 0, 15, 0, 20, 100, 80, 75, 75, 0, 3873 },   // parent 3456
    { 2194597483U, 2196316344U, 1, 1, 0, 0, 0, 0, 0, 30, 30, 0, 100, 0, 586 },   // parent 332
    { 2051003741U, 2285179927U, 1, 1, 0, 0, 45, 0, 0, 100, 80, 110, 0, 40, 1397 },   // parent 755
    { 2969151317U, 2196721375U, 1, 1, 0, 0, 0, 0, 0, 30, 30, 75, 75, 0, 368 },   // parent 368
    { 2628274929U, 624394915U, 5, 5, 0, 0, 300, 0, 20, 30, 30, 110, 0, 40, 6310 },   // parent 6310
    { 2097651668U, 2043093495U, 12, 12, 768, 2304, 15, 2, 20, 100, 80, 110, 0, 40, 16213 },   // parent 14754
    { 272329493U, 37425440U, 1, 2, 0, 0, 0, 5, 0, 100, 80, 75, 75, 15, 4297 },   // parent 1097
    { 136283071U, 2731117957U, 20, 60, 0, 0, 45, 1, 0, 100, 80, 110, 0, 0, 16652 },   // parent 16000
    { 669708539U, 1611129000U, 9, 30, 768, 2304, 15, 5, 0, 30, 30, 0, 0, 40, 8608 },   // parent 6624
    { 1776402276U, 514348265U, 5, 5, 0, 0, 15, 2, 100, 30, 30, 75, 75, 40, 5389 },   // parent 3827
    { 4078482329U, 511958586U, 5, 5, 0, 0, 15, 2, 20, 100, 80, 110, 0, 15, 5203 },   // parent 3328
    { 1321237394U, 478624619U, 5, 5, 0, 0, 0, 5, 100, 30, 30, 110, 0, 40, 7259 },   // parent 3494
    { 568688886U, 651167399U, 5, 5, 0, 0, 300, 5, 20, 30, 30, 75, 75, 0, 12151 },   // parent 5939
};

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
                            for (const auto& abilities : stats)
                                for (const auto& bonus : bonuses)
                                    for (const int gear_pct : offs)
                                        for (int seed = 0; seed < 3; ++seed, ++counter) {
                                            const std::uint32_t seed_low = counter * 2654435761u + 12345u, seed_high = counter * 40503u + 666u + std::uint32_t(seed);
                                            const auto cell = blow_of(seed_low, seed_high, row[0], row[1], holy[0], holy[1], enhanced, n111, dmg_pct, abilities[0], abilities[1], bonus[0], bonus[1], gear_pct);
                                            std::printf("S %u %u %d %d %d %d %d %d %d %d %d %d %d %d | %d %d\n", seed_low, seed_high, row[0], row[1], holy[0], holy[1], enhanced, n111, dmg_pct, abilities[0], abilities[1], bonus[0], bonus[1], gear_pct, cell.hit, cell.phys_fine);
                                        }
        return 0;
    }
    int checked = 0;
    for (const auto& frozen : kFrozen) {
        const auto cell = blow_of(frozen.seed_low, frozen.seed_high, frozen.shield_min, frozen.shield_max, frozen.holy_min, frozen.holy_max, frozen.enhanced, frozen.n111, frozen.dmg_pct, frozen.str, frozen.dex, frozen.str_bonus, frozen.dex_bonus, frozen.gear_pct);
        if (!cell.hit || cell.phys_fine != frozen.phys_fine)
            std::printf("FAIL: shield %d..%d holy %d..%d enhanced %d n111 %d: d2d rolled %d, game.exe's %d\n", frozen.shield_min, frozen.shield_max, frozen.holy_min, frozen.holy_max, frozen.enhanced, frozen.n111, cell.phys_fine, frozen.phys_fine);
        assert(cell.hit && cell.phys_fine == frozen.phys_fine);
        ++checked;
    }
    // The Zakarum Shield (46 46, no percent, seed 1): the maximum is one point above the minimum, so the roll is not always 46.
    int above = 0;
    for (std::uint32_t seed = 1; seed < 200; ++seed) above += blow_of(seed * 2654435761u, seed * 40503u + 666u, 46, 46, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0).phys_fine > 46 * 256;
    assert(above > 0);
    std::printf("test_smite_damage: %d cells match game.exe\n", checked);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "test_smite_damage: %s\n", e.what());
    return 1;
}
