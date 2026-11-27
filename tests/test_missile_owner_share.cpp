// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A SrcDamage missile's owner share (FUN_0064b860 -> FUN_0064aa60): the owner's gear elements, cold
// length and leech scaled by the SrcDamage after Half2HSrc / 128 toward zero. The cases are
// tools/emu/missile_owner_share.py's answers from game.exe; then what player_blow makes of a missile
// swing's fire. No game data needed.
#include <combat.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <iterator>

using namespace d2d::rules;

namespace {

struct Case { int value, srcdam, after; };
constexpr Case kCases[] = {
#include "missile_owner_share_cases.inc"
};

// Each fixture case on every scaled Fighter field; the count of fields that differ.
int fixture_differ() {
    int differ = 0;
    for (const auto& sample : kCases) {
        Fighter fighter;
        for (auto& [low, high] : fighter.elem) low = high = sample.value;
        fighter.cold_len = fighter.life_steal = fighter.mana_steal = sample.value;
        fighter.poison_len = sample.value;
        Swing swing{ .srcdam = sample.srcdam };
        swing.missile = true;
        const auto share = missile_owner_share(fighter, swing);
        int wrong = 0;
        for (const auto& [low, high] : share.elem) wrong += int(low != sample.after) + int(high != sample.after);
        wrong += int(share.cold_len != sample.after) + int(share.life_steal != sample.after) + int(share.mana_steal != sample.after);
        wrong += int(share.poison_len != sample.value);   // +0x3c isn't scaled
        if (wrong > 0 && ++differ <= 5)
            std::printf("differ: value %d srcdam %d: game.exe %d\n", sample.value, sample.srcdam, sample.after);
    }
    std::printf("%zu FUN_0064aa60 cases, %d differ\n", std::size(kCases), differ);
    return differ;
}

// A missile blow's damage from fire alone (no physical, no resistance): the first that lands.
int fire_blow(const Fighter& fighter, const Swing& swing) {
    Rng roll{ 3 };
    auto landed = player_blow(fighter, {}, 2, roll, swing);
    for (int tries = 0; tries < 20 && !landed.hit; ++tries) landed = player_blow(fighter, {}, 2, roll, swing);
    assert(landed.hit);
    return landed.damage;
}

}  // namespace

// The fixture, then Half2HSrc's halved share and player_blow's missile fire.
int main() try {
    int bad = fixture_differ();
    Fighter fighter;
    fighter.attack_rating = 100000;
    fighter.elem[0] = { 100, 100 };
    fighter.two_handed = true;
    Swing swing{ .srcdam = 128 };
    swing.missile = true;
    swing.half_2h_src = true;                              // a two-hander halves 128 to 64 first (0x64bb12)
    if (missile_owner_share(fighter, swing).elem[0].first != 50) { std::puts("FAIL: Half2HSrc share"); ++bad; }
    swing.half_2h_src = false;
    swing.srcdam = 64;
    if (const int damage = fire_blow(fighter, swing); damage != 50) { std::printf("FAIL: missile fire %d, want 50\n", damage); ++bad; }
    swing.missile = false;                                 // melee keeps the gear's whole fire
    if (const int damage = fire_blow(fighter, swing); damage != 100) { std::printf("FAIL: melee fire %d, want 100\n", damage); ++bad; }
    std::puts(bad ? "FAIL" : "OK");
    return bad ? 1 : 0;
} catch (const std::exception& error) {
    std::printf("test_missile_owner_share: %s\n", error.what());
    return 1;
}
