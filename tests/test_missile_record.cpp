// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A missile's physical from its owner's weapon (FUN_0064b860's record, FUN_0064ac60, FUN_005a89a0's hit):
// rules::missile_physical and missile_scaled against game.exe's own run of them (tools/emu/missile_record.py
// inc writes the cases), then what player_blow makes of a missile swing: SrcDamage on the weapon's part only,
// the unit's stats 18 / 17 and the demons' stat 121 in the hit's %.
#include <combat.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

using namespace d2d::rules;

namespace {

// One fixture row: the case's inputs, then game.exe's low, high, record +0x78 and damage (256ths).
struct Case {
    int wmin, wmax, srcdam, row_lo, row_hi, s25, mastery, strength, str_bonus, dex, dex_bonus, missile25, s18, s17, s121, s122, blunt, demon, undead, draw;
    std::int64_t low, high;
    int record_pct;
    std::int64_t damage;
};

// The fixture's cases through missile_physical / missile_scaled, the roll on the case's draw (FUN_0045c3e0 hooked).
int fixture_differ() {
    static const Case cases[]{
#include "missile_record_cases.inc"
    };
    int differ = 0;
    for (const auto& sample : cases) {
        Fighter fighter;
        fighter.phys_lo = sample.wmin * 256;
        fighter.phys_hi = sample.wmax * 256;
        fighter.phys_pct = sample.s25 + sample.mastery + sample.strength * sample.str_bonus / 100 + sample.dex * sample.dex_bonus / 100;
        fighter.unit_min_pct = sample.s18;
        fighter.unit_max_pct = sample.s17;
        fighter.demon_pct = sample.s121;
        fighter.undead_pct = sample.s122;
        fighter.blunt = sample.blunt != 0;
        Swing swing{ .ed_pct = sample.missile25, .srcdam = sample.srcdam, .row_lo = sample.row_lo, .row_hi = sample.row_hi };
        swing.missile = true;
        const Target target{ .undead = sample.undead != 0, .demon = sample.demon != 0 };
        const auto physical = missile_physical(fighter, target, swing);
        const std::int64_t roll = physical.high > physical.low ? physical.low + sample.draw % (physical.high - physical.low) : physical.low;
        const std::int64_t damage = missile_scaled(roll, physical.hit_pct);
        if (physical.low != sample.low || physical.high != sample.high || physical.record_pct != sample.record_pct || damage != sample.damage) {
            if (++differ <= 10)
                std::printf("differ: weapon %d..%d srcdam %d row %d..%d: game.exe %lld..%lld %d %lld, d2d %lld..%lld %d %lld\n", sample.wmin, sample.wmax,
                            sample.srcdam, sample.row_lo, sample.row_hi, (long long)sample.low, (long long)sample.high, sample.record_pct,
                            (long long)sample.damage, (long long)physical.low, (long long)physical.high, physical.record_pct, (long long)damage);
        }
    }
    std::printf("%zu cases, %d differ\n", sizeof(cases) / sizeof(cases[0]), differ);
    return differ;
}

// player_blow's physical on a missile swing: the first blow that lands, on the same draws each time.
int missile_phys(const Fighter& fighter, const Target& target, const Swing& swing) {
    Rng roll{ 3 };
    auto landed = player_blow(fighter, target, 2, roll, swing);
    for (int tries = 0; tries < 20 && !landed.hit; ++tries) landed = player_blow(fighter, target, 2, roll, swing);
    assert(landed.hit);
    return landed.phys;
}

}  // namespace

int main() try {
    assert(fixture_differ() == 0);
    const Target zombie{ .hit_points = 400, .max_hp = 400, .armor_class = 1, .level = 1 };
    const auto bow = simple_fighter(10, 10, 1000000);
    Swing half;
    half.missile = true;
    half.srcdam = 64;
    half.row_lo = half.row_hi = 256;
    // SrcDamage 64 halves the weapon's 10 points (0x64bc43..0x64bc67), not the row's point: 6, not (10 + 1) / 2.
    assert(missile_phys(bow, zombie, half) == 6);
    Swing whole;
    whole.missile = true;
    auto gear = bow;
    gear.unit_max_pct = 50;                          // the unit's stat 17, the larger of 18 / 17 (0x64bdc2..0x64bddc)
    assert(missile_phys(gear, zombie, whole) == 15);
    gear.unit_max_pct = 0;
    gear.demon_pct = 100;                            // stat 121 against a demon only (0x5a8b13..0x5a8b2a)
    assert(missile_phys(gear, zombie, whole) == 10);
    auto demon = zombie;
    demon.demon = true;
    assert(missile_phys(gear, demon, whole) == 20);
    std::puts("OK: a missile's record and hit (FUN_0064b860, FUN_005a89a0)");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
