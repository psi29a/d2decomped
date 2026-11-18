// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// What a monster's elemental and magic blows leave of a player's life when the gear absorbs, from the pieces Fight::hurt_side
// is made of and with no game data: the real rules::monster_blow, Foe::take (which hands each blow the absorb), the Cyclone Armor
// pass over the blows (rules::absorb_blows), the heal-back (rules::absorb_heal) and the totals off the life. The pinned cases are
// game.exe's lives (tools/emu/gear_absorb.py game, FUN_0057c6c0 on a player); test_gear_absorb runs them through Fight::hurt_side.
// One more case runs Energy Shield's pass (rules::energy_shield_blows) with an absorb.

#include <ai.hpp>
#include <combat.hpp>
#include <gear_absorb_case.hpp>
#include <monsters.hpp>
#include <rules.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>

using d2d::game::Foe;
using gear_absorb::Case;
using gear_absorb::parse;

namespace {

// The pieces hurt_side is made of: Foe::take (which hands the blow's parts the absorb), the Cyclone
// Armor pass over the blows (rules::absorb_blows), the heal-back (rules::absorb_heal), the totals off the life (under 0x100 is 0).
std::int64_t life_sim(const Case& one) {
    Foe foe;
    foe.fighter.mdr = int(one.mdr);
    for (std::size_t k = 0; k < 3; ++k) foe.fighter.res[k] = int(one.res[k]);
    constexpr std::array<std::size_t, 4> kTypes{ 0, 1, 2, 4 };
    for (std::size_t k = 0; k < 4; ++k) { foe.fighter.absorb_pct[kTypes[k]] = int(one.pct[k]); foe.fighter.absorb_flat[kTypes[k]] = int(one.flat[k]); }
    for (const auto& blow : one.blows) {
        d2d::rules::MonStats attacker;
        for (std::size_t k = 0; k < blow.fields.size(); ++k)
            attacker.elements[k] = { blow.fields[k].first, 100, blow.fields[k].second, blow.fields[k].second, 0, "A1" };
        d2d::rules::Rng rng{ 1 };
        foe.take(d2d::rules::monster_blow(foe.fighter, 1, d2d::rules::Motion::standing, attacker, false, rng, false, true, true));
    }
    if (one.pool > 0) { int pool = int(one.pool); (void)d2d::rules::absorb_blows(foe.blows, pool, true); }
    std::int64_t life = one.life, total = 0;
    life += d2d::rules::absorb_heal(foe.blows, life, std::int64_t(one.max_life) << 8);
    for (const auto& blow : foe.blows) total += std::max<std::int64_t>(d2d::rules::blow_total(blow), 0);
    life -= total;
    return life < 0x100 ? 0 : life;
}

}   // namespace

int main() {
    for (const auto& [line, life] : gear_absorb::kPins) {
        Case one;
        const bool ok = parse(line, one);
        assert(ok);
        assert(life_sim(one) == life);
    }
    // Energy Shield takes from the raw field first (state event 0xb, before FUN_0057c1e0: test_energy_shield), and the absorb
    // then works on what the shield left: a 100-point cold blow, a shield of 50 % paid 1:1, 20 % cold absorb, the player at 90 of
    // 100. The shield takes 50; the resistance (0) leaves 50; the absorb takes 10 of it, heals 90 -> 100, then 40 comes off: 60.
    {
        Foe foe;
        foe.fighter.absorb_pct[2] = 20;
        d2d::rules::MonStats attacker;
        attacker.elements[0] = { 2, 100, 100, 100, 0, "A1" };
        d2d::rules::Rng rng{ 1 };
        foe.take(d2d::rules::monster_blow(foe.fighter, 1, d2d::rules::Motion::standing, attacker, false, rng, false, true, true));
        d2d::rules::EnergyShield shield;
        shield.pct = 50; shield.per16 = 16; shield.mana = std::int64_t(1000) << 8;
        d2d::rules::energy_shield_blows(foe.blows, shield);
        assert(shield.absorbed == 50 * 256);
        assert(d2d::rules::blow_absorbed(foe.blows[0]) == 10 * 256);
        assert(d2d::rules::blow_total(foe.blows[0]) == 40 * 256);
        const std::int64_t life = 90 * 256;
        assert(life + d2d::rules::absorb_heal(foe.blows, life, 100 * 256) - d2d::rules::blow_total(foe.blows[0]) == 60 * 256);
    }
    std::printf("ok\n");
    return 0;
}
