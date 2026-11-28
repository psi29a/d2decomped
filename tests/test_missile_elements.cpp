// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A SrcDamage missile's record (FUN_0064b860): the skill's element in its field, the owner's element stats
// added (FUN_0064a930: << 8, the poison a frame), all but the poison length x SrcDamage / 128
// (FUN_0064aa60, tools/emu/missile_owner_share.py), each element rolled once (FUN_005a8910) in 256ths,
// resisted less the pierce on that fine value (rules::missile_record, player_blow).
#include <combat.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::rules;

namespace {

// The first blow that lands (the to-hit is a draw).
Blow landed(const Fighter& fighter, const Target& target, const Swing& swing, Rng& roll) {
    auto blow = player_blow(fighter, target, 2, roll, swing);
    for (int tries = 0; tries < 20 && !blow.hit; ++tries) blow = player_blow(fighter, target, 2, roll, swing);
    assert(blow.hit);
    return blow;
}

}  // namespace

int main() try {
    Fighter owner;
    owner.attack_rating = 100000;
    owner.phys_lo = owner.phys_hi = 0;                     // the elements alone
    Swing swing{ .srcdam = 64 };
    swing.missile = true;
    Rng roll{ 3 };

    // 3 points of fire at half: 1.5 points (384 / 256ths), the half point kept (the whole-point scale gave 1).
    owner.elem_stats[0] = { 3, 3 };
    const auto fire = missile_record({}, owner, 64);
    assert(fire.fine[0] == (std::pair{ 384, 384 }));
    auto blow = landed(owner, {}, swing, roll);
    assert(blow.damage == 1 && blow.frac == 128 && blow.elemental);
    std::printf("OK: 3 fire at SrcDamage 64: %d and %d/256\n", blow.damage, blow.frac);

    // The skill's lightning in the record with the owner's, scaled with it: (10 + 2 points) x 96 / 128 = 9.
    owner.elem_stats[0] = {};
    owner.elem_stats[1] = { 2, 2 };
    const MissileDamage bolt{ .etype = 1, .elo = 10 << 8, .ehi = 10 << 8 };
    assert(missile_record(bolt, owner, 96).fine[1] == (std::pair{ 2304, 2304 }));
    swing.srcdam = 96;
    swing.element = bolt;
    blow = landed(owner, {}, swing, roll);
    assert(blow.damage == 9 && blow.frac == 0);
    std::printf("OK: a skill's lightning and the owner's, one roll at SrcDamage 96: %d\n", blow.damage);

    // The poison a frame is scaled, its length isn't: 1 a frame at half over 100 frames = 50.
    owner.elem_stats[1] = {};
    owner.elem_stats[3] = { 256, 256 };
    owner.record_poison_len = 100;
    swing.srcdam = 64;
    swing.element = {};
    const auto poison = missile_record({}, owner, 64);
    assert(poison.fine[3] == (std::pair{ 128, 128 }) && poison.poison_len == 100);
    blow = landed(owner, {}, swing, roll);
    assert(blow.poison == 50 && blow.poison_ticks == 100);
    std::puts("OK: the poison a frame takes the SrcDamage, its length doesn't");

    // The pierce comes off the resistance: 8 points of fire against 50 % less 25 = 6.
    owner.elem_stats[3] = {};
    owner.elem_stats[0] = { 16, 16 };
    Target resisting;
    resisting.res[2] = 50;
    swing.pierce = { 25, 0, 0, 0 };
    blow = landed(owner, resisting, swing, roll);
    assert(blow.damage == 6);
    std::puts("OK: the pierce on the record's fire");

    // A cold range from 0 still chills for its length (the cold field there, no roll of it).
    owner.elem_stats[0] = {};
    swing.pierce = {};
    swing.srcdam = 128;
    swing.element = MissileDamage{ .etype = 2, .elo = 0, .ehi = 256, .elen = 100 };
    blow = landed(owner, {}, swing, roll);
    assert(blow.damage == 0 && blow.chill_ticks == 100);
    std::puts("OK: a cold range from 0 chills for its length");
    swing.element = {};
    swing.srcdam = 64;

    // A burst's units take its first unit's roll (MissileRoll::record).
    owner.elem_stats[0] = { 10, 30 };
    swing.pierce = {};
    MissileRoll shared;
    swing.shared = &shared;
    const auto first = landed(owner, {}, swing, roll);
    assert(shared.record[0] >= 5 << 8 && shared.record[0] < 15 << 8);
    for (int unit = 0; unit < 5; ++unit) assert(landed(owner, {}, swing, roll).damage == first.damage);
    std::puts("OK: a burst's fire is its first unit's roll");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_missile_elements: %s\n", error.what());
    return 1;
}
