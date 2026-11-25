// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A melee record's enhanced damage before its build (FUN_0057b7d0 up to FUN_0057b420): rules::melee_record_pct
// against game.exe's own run of it (tools/emu/melee_record.py inc writes the cases), then player_blow: a
// player's stat 121 adds to the % against a demon only.
#include <combat.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::rules;

namespace {

// One fixture row: the swing's %, the unit's stats 121 / 122, Blunt, the monster's kinds, game.exe's +0xc.
struct Case { int ed_pct, s121, s122, blunt, demon, undead, record_pct; };

// The fixture's cases through melee_record_pct.
int fixture_differ() {
    static const Case cases[]{
#include "melee_record_cases.inc"
    };
    int differ = 0;
    for (const auto& sample : cases) {
        Fighter fighter;
        fighter.demon_pct = sample.s121;
        fighter.undead_pct = sample.s122;
        fighter.blunt = sample.blunt != 0;
        const Swing swing{ .ed_pct = sample.ed_pct };
        const Target target{ .undead = sample.undead != 0, .demon = sample.demon != 0 };
        if (const int got = melee_record_pct(fighter, target, swing); got != sample.record_pct && ++differ <= 10)
            std::printf("differ: ed %d 121 %d 122 %d blunt %d demon %d undead %d: game.exe %d, d2d %d\n", sample.ed_pct, sample.s121, sample.s122,
                        sample.blunt, sample.demon, sample.undead, sample.record_pct, got);
    }
    std::printf("%zu cases, %d differ\n", sizeof(cases) / sizeof(cases[0]), differ);
    return differ;
}

// player_blow's physical: the first blow that lands, on the same draws each time.
int melee_phys(const Fighter& fighter, const Target& target) {
    Rng roll{ 3 };
    auto landed = player_blow(fighter, target, 2, roll);
    for (int tries = 0; tries < 20 && !landed.hit; ++tries) landed = player_blow(fighter, target, 2, roll);
    assert(landed.hit);
    return landed.phys;
}

}  // namespace

int main() try {
    assert(fixture_differ() == 0);
    auto sword = simple_fighter(10, 10, 1000000);
    sword.demon_pct = 100;                           // stat 121 (0x57b857..0x57b86f)
    const Target zombie{ .hit_points = 400, .max_hp = 400, .armor_class = 1, .level = 1 };
    auto demon = zombie;
    demon.demon = true;
    assert(melee_phys(sword, zombie) == 10);
    assert(melee_phys(sword, demon) == 20);
    sword.demon_pct = -50;                           // only when over 0
    assert(melee_phys(sword, demon) == 10);
    std::puts("OK: a melee record's % against demons (FUN_0057b7d0)");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
