// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A weapon's base damage as its stats hold it (FUN_0062cbe0 0x62d495..0x62d6f9 on a load, FUN_0065e4d0 on a new
// ethereal one): low quality's x 3 >> 2 with floors 1 / 2, the throw pair only with a max byte, ethereal's x 3 / 2.
// weapon_base_damage against game.exe's answers (tests/weapon_base_damage_cases.inc, tools/emu/weapon_base_damage.py),
// then make_fighter's melee and throw damage on a low and an ethereal weapon.
#include <combat.hpp>
#include <d2s_items.hpp>
#include <rules.hpp>

#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::rules;

namespace {

// Record bytes +0xfe, +0xff, +0x102, +0x103, +0x100, +0x101, low, ethereal; game.exe's stats 0x15, 0x16, 0x17, 0x18, 0x9f, 0xa0.
struct Case { int min1, max1, min2, max2, min_throw, max_throw, low, ethereal, stat21, stat22, stat23, stat24, stat159, stat160; };
constexpr Case kCases[] = {
#include "weapon_base_damage_cases.inc"
};

// Prints what differs and returns false when `got` isn't `want`.
bool same(const char* what, int index, std::pair<int, int> got, std::pair<int, int> want) {
    if (got == want) return true;
    std::printf("FAIL: case %d %s: d2d %d-%d, game.exe %d-%d\n", index, what, got.first, got.second, want.first, want.second);
    return false;
}

}  // namespace

// Every frozen case through weapon_base_damage, then the fighter's damage off a low and an ethereal axe.
int main() try {
    int index = 0;
    for (const auto& row : kCases) {
        const bool low = row.low != 0, ethereal = row.ethereal != 0;
        if (!same("one-hand", index, weapon_base_damage(row.min1, row.max1, low, ethereal, false), { row.stat21, row.stat22 })
            || !same("two-hand", index, weapon_base_damage(row.min2, row.max2, low, ethereal, false), { row.stat23, row.stat24 })
            || !same("throw", index, weapon_base_damage(row.min_throw, row.max_throw, low, ethereal, true), { row.stat159, row.stat160 }))
            return 1;
        ++index;
    }
    Tables tables;
    tables.item_base["tax"] = { .mindam = 6, .maxdam = 11 };
    tables.item_base["tax"].min_misdam = 9;
    tables.item_base["tax"].max_misdam = 15;
    d2d::d2s::Item axe;
    axe.code = "tax";
    d2d::d2s::Stats stats;
    stats.values[d2d::d2s::kLevel] = 1;
    const StatSum sum{}, weapon_sum{};
    struct Want { int quality; bool ethereal; int min, max, throw_lo, throw_hi; };
    // 6-11 / 9-15: low 4-8 / 6-11; ethereal 9-16 / 13-22; both 6-12 / 9-16.
    constexpr Want kWant[] = { { 2, false, 6, 11, 9, 15 }, { 1, false, 4, 8, 6, 11 }, { 2, true, 9, 16, 13, 22 }, { 1, true, 6, 12, 9, 16 } };
    for (const auto& want : kWant) {
        axe.quality = want.quality;
        axe.ethereal = want.ethereal;
        const auto fighter = make_fighter(tables, &axe, nullptr, sum, weapon_sum, stats, {}, 0, {});
        const auto thrown = make_fighter(tables, &axe, nullptr, sum, weapon_sum, stats, {}, 0, {}, nullptr, true);
        if (fighter.min != want.min || fighter.max != want.max || fighter.throw_lo != want.throw_lo || fighter.throw_hi != want.throw_hi
            || thrown.min != want.throw_lo || thrown.max != want.throw_hi) {
            std::printf("FAIL: quality %d ethereal %d: melee %d-%d throw %d-%d (thrown %d-%d), want %d-%d / %d-%d\n", want.quality, int(want.ethereal),
                        fighter.min, fighter.max, fighter.throw_lo, fighter.throw_hi, thrown.min, thrown.max, want.min, want.max, want.throw_lo, want.throw_hi);
            return 1;
        }
    }
    std::printf("OK: %d FUN_0062cbe0 base damage cases, make_fighter on low and ethereal\n", index);
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
