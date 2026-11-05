// SPDX-License-Identifier: GPL-3.0-or-later
// Combat: the player (or merc) as a Fighter built from gear and stats, one
// blow each way (hit chance, block and the defender's rolls, damage,
// resistances, crushing / deadly / critical strike, leech, poison, chill),
// swing speed breakpoints, experience and levels. Traced in game.exe where
// noted: docs/research/re/combat.md.
#pragma once

#include "monsters.hpp"
#include "rules.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace d2d::rules {

// Chance to hit in percent (FUN_0057d9b0): negative defense adds to the
// attack rating (and vice versa); c = AR x 100 / (AR + DEF), then
// c x 2 x alvl / (alvl + dlvl), clamped to 5..95.
inline int hit_chance(int attack_rating, int def, int alvl, int dlvl) {
    std::int64_t attack = attack_rating, defense = def;
    if (defense < 0) { attack -= defense; defense = 0; }
    if (attack < 0) { defense -= attack; attack = 0; }
    const std::int64_t chance = attack + defense != 0 ? attack * 100 / (attack + defense) : 100;
    return int(std::clamp<std::int64_t>(chance * 2 * alvl / std::max(alvl + dlvl, 1), 5, 95));
}

// Item stats summed by ItemStatCost id: what's worn, charms carried, and
// what's socketed in them (the caller resolves sockets).
using StatSum = std::array<std::int64_t, 512>;

// The player in a fight: what an attack does and what protects them.
// Elements index 0 fire, 1 lightning, 2 cold, 3 poison, 4 magic.
struct Fighter {
    int min = 1, max = 2, attack_rating = 1;                   // physical damage, attack rating (as the panel shows them)
    // Before their percentages, so a skill's % joins the same sum as in
    // game.exe: the weapon damage in 256ths and its % (FUN_0057b420), the
    // attack rating and its % (FUN_0057d9b0).
    int phys_lo = 256, phys_hi = 512, phys_pct = 0;
    int ar_base = 1, ar_pct = 0;
    // Kicks (FUN_00646280): stat 137 + the boots' kick damage, and its % —
    // the boots' StrBonus / DexBonus on strength / dexterity, + 25, + 17.
    int kick_lo = 0, kick_hi = 0, kick_pct = 0;
    int smite_lo = 0, smite_hi = 0, smite_pct = 0;  // the shield's (Smite, FUN_005ce9f0 -> FUN_0057b420 on the shield)
    bool blunt = false;                             // the weapon is a Blunt type (ItemTypes Equiv chain: club, hammer, mace, scepter, wand, staff)
    int undead_pct = 0;                             // stat 122 (item_undeaddamage_percent) over all the gear (the game reads the unit's own stat)
    std::array<std::pair<int, int>, 5> elem{};      // added damage (poison: its total over poison_len)
    int cold_len = 0, poison_len = 0;               // ticks
    int stun_len = 0;                               // stat 66 (gear), ticks
    int crushing = 0, deadly = 0, critical = 0, open_wounds = 0;   // chances, %
    int mastery_crit = 0;                           // stat 344, the weapon's mastery (FUN_00645830)
    int weapon_block = 0;                           // stat 348, two claws only (FUN_0057dca0)
    bool knockback = false;
    bool prevent_heal = false;                      // stat 117 (item_preventheal)
    int hit_class = 1;                              // the weapon's (Weapons.txt hit class; 1 hand to hand), FUN_00623c20
    int life_steal = 0, mana_steal = 0;             // %
    int ias = 0, wsm = 0, frw = 0, fhr = 0, fbr = 0, fcr = 0;
    int velocity_pct = 0;   // stat 0x43 velocitypercent on top of the 100 (Vigor, Burst of Speed, ...)
    int defense = 0, block = 0;                     // block %, standing still
    int def_melee = 0, def_missile = 0;             // stats 33 / 32: extra defense vs each
    int dodge = 0, avoid = 0, evade = 0;            // stats 338 / 339 / 340, %
    int dr_pct = 0, dr_flat = 0, mdr = 0;
    std::array<int, 4> res{};                       // fire, lightning, cold, poison, %
    int plr = 0;                                    // poison length reduced, % (stat 110 + the difficulty's penalty, at most 75)
    bool half_freeze = false, cannot_freeze = false; // stats 118 / 153
    int thorns = 0, thorns_light = 0;               // attackers take (melee)
    int thorns_pct = 0;                             // stat 131: % of a melee hit's damage back (Thorns)
    int life_regen = 0, mana_regen = 0;             // hpregen; manarecoverybonus %
};

// Builds the Fighter from the worn weapon / shield, the summed item stats
// (`sum`: everything worn and carried; `weapon_sum`: the weapon and what's
// socketed in it), the character's stats and class; defense and
// resistances come from the char panel's sums.
//   damage (FUN_0057b420): the weapon's min/max with its own enhanced
//     damage 18 / 17 (+219 per level) — op 13 stats, which only touch the
//     item they're on — plus 21 / 22 (+218 per level) flat and 111 on both;
//     then one percentage: 25 damagepercent + str x StrBonus / 100 +
//     dex x DexBonus / 100 (at least -90). Barehanded 1-2 and no stat bonus.
//   attack rating (FUN_00622560): (dex - 7) x 5 + ToHitFactor + 19 (+224
//     per level), x (1 + 119 %)
//   block (FUN_00622720, a shield only): (shield block + BlockFactor + 20) x
//     (dex - 15) / (clvl x 2), at most 75
//   elements: 48/49 fire, 50/51 lightning, 54/55 cold (56 ticks), 57/58
//     poison per tick in 256ths (59 ticks), 52/53 magic
//   136 crushing blow, 141 (+250 per level) deadly strike, 337 critical
//   strike, 135 open wounds, 81 knockback, 60/62 life/mana steal, 93 IAS
//   (weapon speed WSM), 96 FRW, 99 FHR, 102 FBR, 33 / 32 defense vs melee /
//   missiles, 338 / 339 / 340 dodge / avoid / evade, 36 damage reduced %
//   (at most 50), 34 flat, 35 magic, 78 / 128 attacker takes damage /
//   lightning, 74 replenish life, 27 mana regeneration %.
//   masteries (FUN_00645830), in `sum` only when the weapon is their
//     passiveitype (the caller checks): 342 to-hit joins 119, 343 damage
//     joins 25, 344 crit rolls first; 348 weapon block (two claws).
inline Fighter make_fighter(const Tables& tables, const d2d::d2s::Item* weapon, const d2d::d2s::Item* shield,
                            const StatSum& sum, const StatSum& weapon_sum, const d2d::d2s::Stats& stats,
                            const ClassGains& gains, int defense, const std::array<int, 4>& res,
                            const d2d::d2s::Item* boots = nullptr, bool thrown = false) {
    using namespace d2d::d2s;
    Fighter fighter;
    const auto stat = [&](int id) { return sum[std::size_t(id)]; };
    const auto weapon_stat = [&](int id) { return weapon_sum[std::size_t(id)]; };
    const std::int64_t clvl = std::max<std::int64_t>(stats.get(kLevel), 1), str = stats.get(kStr), dex = stats.get(kDex);
    const ItemBase* weapon_base = nullptr;
    if (weapon && !broken(*weapon)) if (const auto found = tables.item_base.find(weapon->code); found != tables.item_base.end()) weapon_base = &found->second;   // a broken one's stats are off (FUN_0055f850)
    if (weapon) if (const auto found = tables.item_base.find(weapon->code); found != tables.item_base.end() && found->second.hit_class) fighter.hit_class = found->second.hit_class;
    // In 256ths, as game.exe keeps them.
    // `thrown`: the weapon's throw damage (FUN_0064b860: stats 0x9f / 0xa0, minmisdam / maxmisdam).
    const int base_min = weapon_base ? (thrown ? weapon_base->min_misdam : weapon_base->mindam) : 0, base_max = weapon_base ? (thrown ? weapon_base->max_misdam : weapon_base->maxdam) : 0;
    std::int64_t low = (weapon_base ? base_min * (100 + weapon_stat(kMinDamagePercent)) / 100 : 1) + stat(kMinDamage) + stat(kNormalDamage),
                 high = (weapon_base ? base_max * (100 + weapon_stat(kMaxDamagePercent) + weapon_stat(kMaxDamagePercentPerLevel) * clvl / 8) / 100 : 2) + stat(kMaxDamage) + stat(kMaxDamagePerLevel) * clvl / 8 + stat(kNormalDamage);
    low = std::max<std::int64_t>(low, 1) << 8;
    high = std::max<std::int64_t>(high << 8, low + 256);
    const std::int64_t pct = std::max<std::int64_t>(stat(kDamagePercent) + stat(kPassiveMasteryMeleeDamage) + (weapon_base ? str * weapon_base->str_bonus / 100 + dex * weapon_base->dex_bonus / 100 : 0), -90);
    fighter.phys_lo = int(low); fighter.phys_hi = int(high); fighter.phys_pct = int(pct);
    if (weapon_base) {
        const auto info = tables.item_info.find(weapon->code);
        fighter.blunt = (info != tables.item_info.end() && type_is(tables, info->second.type, "blun")) || (!weapon_base->type2.empty() && type_is(tables, weapon_base->type2, "blun"));
    }
    fighter.undead_pct = int(stat(kUndeadDamagePercent));
    fighter.min = int(std::max<std::int64_t>((low + low * pct / 100) >> 8, 1));
    fighter.max = int(std::max<std::int64_t>((high + high * pct / 100) >> 8, fighter.min));
    fighter.ar_base = int(dex * 5 - 35 + gains.to_hit + stat(kToHit) + stat(kToHitPerLevel) * clvl / 8);
    fighter.ar_pct = int(stat(kToHitPercent) + stat(kPassiveMasteryMeleeToHit));
    fighter.attack_rating = int(std::max<std::int64_t>(std::int64_t(fighter.ar_base) * (100 + fighter.ar_pct) / 100, 1));
    fighter.kick_lo = fighter.kick_hi = int(stat(kKickDamage));
    if (boots) if (const auto found = tables.item_base.find(boots->code); found != tables.item_base.end()) {
        fighter.kick_lo += found->second.mindam;
        fighter.kick_hi = std::max(fighter.kick_hi + found->second.maxdam, fighter.kick_lo);
        fighter.kick_pct = int(std::max<std::int64_t>(str * found->second.str_bonus / 100 + dex * found->second.dex_bonus / 100 + stat(kDamagePercent), -90)
                         + stat(kMaxDamagePercent) - weapon_stat(kMaxDamagePercent));
    }
    if (shield) if (const auto found = tables.item_base.find(shield->code); found != tables.item_base.end()) {
        fighter.smite_lo = found->second.mindam;
        fighter.smite_hi = std::max(found->second.maxdam, fighter.smite_lo);
        fighter.smite_pct = int(std::max<std::int64_t>(str * found->second.str_bonus / 100 + dex * found->second.dex_bonus / 100 + stat(kDamagePercent), -90)
                          + stat(kMaxDamagePercent) - weapon_stat(kMaxDamagePercent));
    }
    if (shield) if (const auto found = tables.item_base.find(shield->code); found != tables.item_base.end() && found->second.block > 0)
        fighter.block = int(std::clamp<std::int64_t>((found->second.block + gains.block + stat(kToBlock)) * (dex - 15) / (clvl * 2), 0, 75));
    // Each element's mastery % (FUN_0057b7d0 -> FUN_0057a8e0): fire 329,
    // lightning 330, cold 331, poison 332, magic 357.
    const auto scaled = [&](std::int64_t value, int stat_id) { return int(value * (100 + stat(stat_id)) / 100); };
    fighter.elem = { { { scaled(stat(kFireMinDamage), kPassiveFireMastery), scaled(stat(kFireMaxDamage), kPassiveFireMastery) }, { scaled(stat(kLightningMinDamage), kPassiveLightningMastery), scaled(stat(kLightningMaxDamage), kPassiveLightningMastery) }, { scaled(stat(kColdMinDamage), kPassiveColdMastery), scaled(stat(kColdMaxDamage), kPassiveColdMastery) },
                 { scaled(stat(kPoisonMinDamage) * stat(kPoisonLength) / 256, kPassivePoisonMastery), scaled(stat(kPoisonMaxDamage) * stat(kPoisonLength) / 256, kPassivePoisonMastery) }, { scaled(stat(kMagicMinDamage), kPassiveMagicMastery), scaled(stat(kMagicMaxDamage), kPassiveMagicMastery) } } };
    fighter.cold_len = int(stat(kColdLength)); fighter.poison_len = int(stat(kPoisonLength));
    fighter.stun_len = int(stat(kStunLength));
    fighter.crushing = int(stat(kCrushingBlow)); fighter.deadly = int(stat(kDeadlyStrike) + stat(kDeadlyStrikePerLevel) * clvl / 8); fighter.critical = int(stat(kPassiveCriticalStrike));
    fighter.mastery_crit = int(stat(kPassiveMasteryMeleeCritical)); fighter.weapon_block = int(stat(kPassiveWeaponBlock));
    fighter.open_wounds = int(stat(kOpenWounds));
    fighter.knockback = stat(kKnockback) > 0;
    fighter.prevent_heal = stat(kPreventHeal) > 0;
    fighter.life_steal = int(stat(kLifeDrainMinDamage)); fighter.mana_steal = int(stat(kManaDrainMinDamage));
    fighter.ias = int(stat(kFasterAttackRate)); fighter.wsm = weapon_base ? weapon_base->speed : 0; fighter.frw = int(stat(kFasterMoveVelocity)); fighter.fhr = int(stat(kFasterGetHitRate)); fighter.fbr = int(stat(kFasterBlockRate)); fighter.fcr = int(stat(kFasterCastRate));
    fighter.defense = defense;
    fighter.def_melee = int(stat(kArmorClassVsHandToHand)); fighter.def_missile = int(stat(kArmorClassVsMissile));
    fighter.dodge = int(stat(kPassiveDodge)); fighter.avoid = int(stat(kPassiveAvoid)); fighter.evade = int(stat(kPassiveEvade));
    fighter.dr_pct = int(std::min<std::int64_t>(stat(kDamageResist), 50)); fighter.dr_flat = int(stat(kNormalDamageReduction)); fighter.mdr = int(stat(kMagicDamageReduction));
    fighter.res = res;
    fighter.thorns = int(stat(kAttackerTakesDamage)); fighter.thorns_light = int(stat(kAttackerTakesLightningDamage)); fighter.thorns_pct = int(stat(kThornsPercent));
    fighter.life_regen = int(stat(kHitPointRegeneration)); fighter.mana_regen = int(stat(kManaRecoveryBonus));
    return fighter;
}

// A fighter with just damage, attack rating and defense (the merc, its
// arrows, tests).
inline Fighter simple_fighter(int min, int max, int attack_rating, int defense = 0) {
    Fighter fighter;
    fighter.min = min; fighter.max = std::max(max, min); fighter.attack_rating = fighter.ar_base = attack_rating; fighter.defense = defense;
    fighter.phys_lo = min << 8; fighter.phys_hi = fighter.max << 8;
    return fighter;
}

// Diminishing returns on the speed stats: IAS / FHR / FBR count
// 120 x v / (120 + v), FRW 150 x v / (150 + v).
inline int effective_speed(int speed, int cap = 120) { return speed > 0 ? cap * speed / (cap + speed) : speed; }

// A player's walk / run / town-walk animation rate, 1/256 frames a tick
// (FUN_00623f50 on a PlrMode +4 mode): FUN_006213d0's base, RN 0x65, WL and
// TW 0xd5 (AnimData's speed isn't read), x the walk % (velocitypercent +
// effective FRW, at least 25) / 100, clamped to 0..0x7fff. The client's
// player tick (FUN_00463390) steps WL / RN / TW by it (FUN_00623e00).
inline int moving_anim_rate(bool run, int walk_percent) {
    return std::clamp((run ? 0x65 : 0xd5) * std::max(walk_percent, 25) / 100, 0, 0x7fff);
}

// Ticks an attack animation of `frames` frames at AnimData `rate` takes with
// `ias` and weapon speed `wsm` (1.10): EIAS = effective IAS - WSM, clamped
// to -85..75; ticks = ceil(256 x frames / (rate x (100 + EIAS) / 100)).
inline int attack_speed(int rate, int ias, int wsm, int sias = 0) {
    const int eias = std::clamp(effective_speed(ias) - wsm + sias, -85, 75);
    return std::max(rate * (100 + eias) / 100, 1);
}
inline int attack_ticks(int frames, int rate, int ias, int wsm, int sias = 0) {
    const int speed = attack_speed(rate, ias, wsm, sias);
    return (256 * frames + speed - 1) / speed;
}

// Frame count after a speed stat (FCR / FHR / FBR) shortens `base_frames`:
// E = 120 v / (120 + v); shown = ceil(base x 256 / (256 + floor(256 E / 100))).
// Emergent breakpoints — Sorc SC base 14 sweeps 14/13/12/11/10/9/8 at
// stat 0/9/20/37/63/105/200.
inline int speed_frames(int base_frames, int speed_stat) {
    if (base_frames <= 0 || speed_stat <= 0) return std::max(base_frames, 0);
    const int scaled = 256 * effective_speed(speed_stat) / 100;
    return (base_frames * 256 + 256 + scaled - 1) / (256 + scaled);
}

// The get-hit test on a landed hit (FUN_0057cb00, called by FUN_0057cee0 on
// result flag 4 unless the unit is stunned: the player branch at 0x57d37a,
// the monster's at 0x57d0c8). No get-hit for a frozen unit (state 1), a hit
// that is all poison (record [10] == [0x13]) or one under a point; else the
// divisor by the record's hit class, the jump table at 0x57cbf8: 8 for
// classes 2, 6, 10, 11; 32 for 4, 8; 64 for 5; 16 for 1, 3, 7, 9, 12 and any
// other value. FUN_0057ce30 fixes the class up first: 13 for none, and an
// element's bits (0x20 .. 0x50 when the record's fire, lightning, cold or
// poison is over 0) or a doubled hit's 0x10 (record flag 0x2000, 0x57b929: the
// mastery crit, critical strike or deadly strike roll of FUN_0057b7d0 landed)
// OR-ed in, which puts it past 12: a blow that carries an element divides by
// 16 whatever the weapon. Under max / d: none; under 2 max / d: a roll of 2 on
// the unit's seed (FUN_00472210) of 0 is none; then under 4 max / d: a roll of
// 4 of 0 is none; else a get-hit. So the bands: 37.5 % from max / d, 75 % from
// 2 max / d, sure from 4 max / d. A monster also needs MonStats2's GH mode
// (FUN_0046c140 bit 3). Its oracle (hit_rule.py) was never committed: cited, not re-checkable.
// `max_life` and `damage` in 256ths; `marked`: an element or a doubled hit.
inline int get_hit_divisor(int hit_class) {
    switch (hit_class) {
    case 2: case 6: case 10: case 11: return 8;
    case 4: case 8: return 32;
    case 5: return 64;
    default: return 16;
    }
}
inline bool get_hit(std::int64_t max_life, std::int64_t damage, int hit_class, bool marked, Rng& rng) {
    if (damage < 256) return false;
    const int divisor = marked ? 16 : get_hit_divisor(hit_class);
    if (damage < max_life / divisor) return false;
    if (damage < max_life / (divisor / 2) && rng(2) == 0) return false;
    if (damage < max_life / (divisor / 4) && rng(4) == 0) return false;
    return true;
}

// FUN_0057bf80 on a damage record's physical field, in 256ths: the flat
// reduction (stat 34, whole points) comes off first, then the percent (stat 36)
// of what is left; a field already at 0 stays 0, the product truncates toward
// zero (FUN_00483360). reduce_fine clamps a result under 0; game.exe stores it, sums it into the
// record total and only ignores a total not above 0 (0057c852): reduce_fine_signed keeps it.
// ponytail: FUN_0057c1e0 scales the flat by the record's +0x54 / 1024 when that
// is above 0 and the whole record by FUN_0057c060's percent; neither is modelled.
inline int reduce_fine_signed(int fine, int percent, int flat) {
    if (fine <= 0) return 0;
    fine -= flat * 256;
    if (fine > 0 && percent != 0) fine = int(std::int64_t(fine) * (100 - std::clamp(percent, -100, 100)) / 100);
    return fine;
}
inline int reduce_fine(int fine, int percent, int flat) { return std::max(reduce_fine_signed(fine, percent, flat), 0); }
// One landed blow on a unit this frame, as game.exe keeps one damage record a
// hit: its damage (whole points), hit class and element mark (an element or
// poison, Foe::take), and of the damage the physical part (record +0x08) and
// the fire / lightning / cold part (+0x10, +0x1c, +0x24) an absorb reads.
struct StruckBlow {
    int damage = 0, hit_class = 0;
    bool marked = false;
    int physical = 0, elemental = 0;
    bool poisoned = false;
    int frac = 0;                        // 256ths of a point the record total carries beyond `damage` (Taken::frac)
    // The physical field before the defender's reduction, in 256ths, and the reduction (percent, flat) to put back on it:
    // game.exe absorbs first (state event 0xb, 0x57c2fc) and reduces after (the loop at 0x57c378). -1: not known,
    // the physical and `frac` are taken as they are (nothing to reduce again).
    int raw = -1, pct = 0, flat = 0;
    // The fire / lightning / cold / magic parts before the defender's resistance (Taken::element_raw, by monster_blow's
    // element type: 0 fire, 1 lightning, 2 cold, 4 magic; 3 is poison, which has no record field an absorb reads): the rolls
    // summed (whole points), what the blow carries of them now (`dealt`, whole points) and the resistance they went through.
    // An Energy Shield works on these, as game.exe's absorb event runs before the resistance pass (energy_shield_blows).
    // `fine`: the field as the resistance pass stores it, in 256ths and possibly under 0 (kNoFine: not known, `dealt` x 256 stands).
    static constexpr int kNoFine = std::numeric_limits<int>::min();
    struct Part { int raw = 0, dealt = 0, res = 0, fine = kNoFine; };
    std::array<Part, 5> parts{};
    int mdr = 0;                         // the flat magic damage reduction taken off each of them before the resistance
    // The record total (+0x4c) in 256ths BEFORE it is clamped, when monster_blow built the blow from its fields: `damage` and
    // `frac` hold it clamped at 0. Unset: damage x 256 + frac is the total (a blow built by hand or by another path).
    std::optional<int> sum;
};
inline int part_fine(const StruckBlow::Part& part) { return part.fine != StruckBlow::kNoFine ? part.fine : part.dealt * 256; }
// The record total in 256ths, unclamped when known.
inline std::int64_t blow_total(const StruckBlow& blow) { return blow.sum ? *blow.sum : (std::int64_t(blow.damage) << 8) + blow.frac; }
// The physical field now, in 256ths: under 0 when the blow keeps its fields signed (`sum`), else as `damage` holds it (clamped).
inline std::int64_t physical_now(const StruckBlow& blow) {
    if (blow.raw < 0) return blow.physical * 256 + blow.frac;
    const std::int64_t fine = reduce_fine_signed(blow.raw, blow.pct, blow.flat);
    return blow.sum ? fine : std::max<std::int64_t>(fine, 0);
}
inline void store_total(StruckBlow& blow, std::int64_t total) {
    if (blow.sum || total < 0) blow.sum = int(total);
    total = std::max(total, std::int64_t(0));
    blow.damage = int(total >> 8);
    blow.frac = int(total & 255);
}

// Puts a blow's reduced physical (`physical`, and its share of the total) back in line with `raw`; `old` is the physical field it had
// (physical_now before `raw` changed).
inline void settle_blow(StruckBlow& blow, std::int64_t old) {
    if (blow.raw < 0) return;
    const std::int64_t fine = reduce_fine_signed(blow.raw, blow.pct, blow.flat);
    store_total(blow, blow_total(blow) + fine - old);
    blow.physical = int(std::max<std::int64_t>(fine, 0) >> 8);
}

// A state's absorb pool on the frame's blows: FUN_0057c1e0 raises state event
// 0xb (FUN_005c0c30, 0x57c2fc) on each record before it sums the total +0x4c,
// so the pool empties record by record in landing order and each blow loses
// only its own part: Bone Armor (FUN_005c6370) from the physical field,
// Cyclone Armor (FUN_005c87a0) from fire, cold, then lightning. A blow whose
// fire / lightning / cold went keeps its mark only by its poison
// (FUN_0057ce30 reads the fields after). `pool` is in 256ths, as game.exe's
// stat is: Bone Armor subtracts it from the physical field BEFORE the defender's
// reduction (`raw`; a blow without one takes it from `physical` and `frac`), then
// the flat and percent reduction (reduce_fine) are put back on what is left, as
// game.exe does (absorb event 0xb at 0x57c2fc, the loop at 0x57c378). A pool that
// outlasts the field takes the fraction too. Returns what the pool took, in 256ths
// of the unreduced field.
// Its oracle for the pool in 256ths and the absorb before the reduction is tools/emu/monster_damage.py (sets absorb and
// both); hit_absorb.py, the older whole-point one, was never committed.
// ponytail: the physical reduction is put back after the absorb (raw); fire / lightning / cold are already resisted in d2d's blows, game.exe resists after.
// ponytail: Cyclone Armor's fields are whole points here (no elemental fraction), so it takes whole points from the pool.
inline int absorb_blows(std::span<StruckBlow> blows, int& pool, bool elemental) {
    int taken = 0;
    for (auto& blow : blows) {
        if (elemental) {
            const int cut = std::min({ pool >> 8, blow.elemental, blow.damage });
            if (cut <= 0) continue;
            blow.elemental -= cut; store_total(blow, blow_total(blow) - cut * 256); pool -= cut * 256; taken += cut * 256;
            if (blow.elemental <= 0 && !blow.poisoned) blow.marked = false;
            continue;
        }
        const std::int64_t before = physical_now(blow);
        if (blow.raw < 0) blow.raw = blow.physical * 256 + blow.frac;
        const int cut = std::min(pool, blow.raw);
        if (cut <= 0) continue;
        blow.raw -= cut;
        settle_blow(blow, before);
        pool -= cut; taken += cut;
    }
    return taken;
}

// FUN_0057cb00 once per landed blow (FUN_0057c6c0 then FUN_0057cee0 per
// record): each blow rolls on what is left of it, its own class and mark; a
// get-hit if any is.
inline bool struck_big(std::span<const StruckBlow> blows, std::int64_t max_life, Rng& seed) {
    bool big = false;
    for (const auto& blow : blows)
        if (blow.damage > 0 && get_hit(max_life, std::int64_t(blow.damage) << 8, blow.hit_class, blow.marked, seed)) big = true;
    return big;
}

// The get-hit's animation rate, 1/256 frames a tick (FUN_00623f50's get-hit
// branch, 0x62409f..0x624385, a player's mode 4 or a monster's mode 3): the
// AnimData speed x (50 + the effective FHR, FUN_00621740's 120 v / (120 + v)
// of stat 99 for v != 0, truncated toward zero) / 100. Its multiply and divide
// are unsigned (0x62435c..0x624368), so a negative factor wraps and normally
// clamps to 0x7fff. Chill, attackrate and other_animrate aren't read there.
// At v = -120 game.exe's IDIV divides by zero; d2d leaves that v unchanged.
inline int get_hit_rate(int speed, int fhr) {
    const int recovery = fhr != 0 && fhr != -120 ? 120 * fhr / (120 + fhr) : fhr;
    const std::uint32_t product = std::uint32_t(speed) * std::uint32_t(50 + recovery);
    return int(std::min(product / 100, std::uint32_t{ 0x7fff }));
}

// How many server ticks the get-hit mode holds (FUN_005539b0, the mode's
// events, from FUN_0057fe30 / FUN_005a7c20): from frame 0 it adds the rate
// a tick while the frame stays under frames x 256, held such ticks, and queues
// the mode end (event 1: FUN_00581020 plays NU, a monster's FUN_005a8030) at
// the game frame + max(held, 1) + 1; a rate of 0 ends it the next frame.
// Sorceress SOGHHTH (8 frames at 256): 16 / 15 / 14 / 13 / 12 / 11 / 10 / 9 /
// 8 / 7 / 6 ticks at FHR 0 / 5 / 9 / 14 / 20 / 30 / 42 / 60 / 86 / 142 / 280.
// The record's own event frames (AnimData +0x10, read in the same loop)
// queue only type-0 events, never the end, and in a get-hit those do
// nothing: a player's (event table 0x6e1810 -> FUN_005811d0, mode 4's
// action 0x732c10[4] = FUN_0057fe20) returns 1, a monster's (mode 3's
// action FUN_005a6de0) drops the unit's type-0 events. 11 of 1.14d's 187
// GH records have one (frame 0: BAGH 1HT / 2HS / 2HT / XBW, IP, SL, ...).
inline int get_hit_ticks(int frames, int speed, int fhr) {
    const int rate = get_hit_rate(speed, fhr);
    if (rate <= 0) return 1;
    const int held = frames > 0 ? (frames * 256 - 1) / rate : 0;
    return std::max(held, 1) + 1;
}

// The frame game.exe's client draws `tick` ticks into a get-hit (tick 0 the
// mode set): the client steps the unit's 8.8 frame position by its rate a
// tick while position + rate stays under frames x 256 (FUN_004b1280 on the
// monster mode table's get-hit row at 0x7252e0 + 3 x 12, [0, 1, 2]:
// FUN_006217c0's "over", else FUN_00623e00's step; a player's FUN_00463390
// at 0x463487 the same) and the draw shows position >> 8 (FUN_00621810), so
// the animation plays once at the get-hit rate and stands on its last stepped
// frame for the rest of the hold; it never loops. A rate of 0 stays on 0.
inline int get_hit_frame(int frames, int rate, int tick) {
    if (rate <= 0 || frames <= 0 || tick <= 0) return 0;
    const int held = (frames * 256 - 1) / rate;
    return std::min(tick, held) * rate / 256;
}

// A cast animation's speed, 1/256 frames a tick (FUN_00623f50's cast branch at
// 0x623fcb: FUN_00621740 turns stat 105 into the effective FCR through the row
// at 0x6e8e3c): AnimData speed x min(100 + effective FCR, 175) / 100, clamped
// to 0..0x7fff.
inline int cast_speed(int rate, int fcr) {
    return std::clamp(rate * std::min(100 + effective_speed(fcr), 175) / 100, 0, 0x7fff);
}

// Client ticks a plain cast (a mode with no sequence) lasts. FUN_00463390 asks
// FUN_006217c0 after each step whether `current + speed >= frames x 256`, so the
// last step is never taken: ceil(256 x frames / speed) - 1 ticks, at least one
// (tools/emu/cast_rates.py runs game.exe's code for it).
inline int cast_ticks(int frames, int rate, int fcr) {
    if (frames <= 0) return 0;
    const int speed = std::max(cast_speed(rate, fcr), 1);
    return std::max((256 * frames + speed - 1) / speed - 1, 1);
}

// The tick of a cast mode's action frame, counted from its start: the animation scheduler
// (FUN_005539b0) files the event on the first tick whose position (steps of the cast speed
// from 0) reaches the frame (tools/emu/cast_mana.py runs game.exe's code for it, V run, for the
// first Sorceress SC record only; other classes' SC records and A1-mode casts such as Magic Arrow
// are not in the oracle, U).
inline int cast_action_ticks(int frame, int rate, int fcr) {
    return std::max(256 * frame + std::max(cast_speed(rate, fcr), 1) - 1, 0) / std::max(cast_speed(rate, fcr), 1);
}

// Open wounds: bleeding per second for a character level (1.10's table in
// 256ths a tick, times 25 ticks), for 8 seconds.
inline int open_wounds_per_sec(int clvl) {
    const int value = clvl < 15 ? 9 * clvl + 31 : clvl < 31 ? 18 * clvl - 104 : clvl < 46 ? 27 * clvl - 374
                : clvl < 61 ? 36 * clvl - 779 : 45 * clvl - 1319;
    return std::max(value, 0) * 25 / 256;
}

// A monster being struck: its life now and max, defense, level, block
// chance (0 unless it can block), resistances (physical, magic, fire,
// lightning, cold, poison %), Drain (the % of leech that works on it),
// Crushing Blow's life divisor (FUN_005bffc0: Monster::crush_divisor).
struct Target {
    int hit_points = 1, max_hp = 1, armor_class = 0, level = 1, block = 0;
    std::array<int, 6> res{};
    int drain = 100;
    int crush_divisor = 4;
    bool undead = false;                            // MonStats lUndead or hUndead (FUN_0063e990)
    bool demon = false;                             // MonStats demon (FUN_0063e940)
};
// What one player hit does: the instant damage (physical after resistance,
// fire / lightning / cold / magic, crushing blow), leeched life and mana,
// poison (total and ticks), chill ticks, and whether it bled, knocked back.
struct Blow {
    bool hit = false, blocked = false, crushing = false, deadly = false, bleed = false, knockback = false;
    int damage = 0, life = 0, mana = 0, poison = 0, poison_ticks = 0, chill_ticks = 0;
    int phys = 0;                        // the physical rolled, before resistance (Dragon Tail's fire is a share of it)
    int phys_fine = 0;                   // the physical after resistance, in 256ths (the record's +8 field)
    int frac = 0;                        // 256ths of a point the physical (a blow's fine value after resistance) and a missile's element dealt beyond `damage`; may pass 255 (game.exe keeps life in 256ths)
    int stun_ticks = 0;
    int hit_class = 0;                   // what the struck unit's impact sound is (sound_ids::impact)
    bool elemental = false;              // fire, lightning, cold or poison got through (not magic): the get-hit divides by 16 (get_hit)
    bool prevent_heal = false;           // the to-hit roll passed with stat 117 (FUN_0057d9b0 -> FUN_0057d810), blocked or not
};
inline int resisted(int dmg, int res) { return res >= 100 ? 0 : dmg * (100 - res) / 100; }
// game.exe's MulDiv (FUN_00483360): value x times / over, truncated, 0 over
// 0. Up to 0x100000 with times up to 0x10000 it multiplies in 32 bits (the
// product wraps); a bigger times over at most times / 16 divides it first,
// as a bigger value over at most value / 16 does the value (each multiply
// wrapping); else 64 bits.
inline int mul_div(int value, int times, int over) {
    if (over == 0) return 0;
    if (value <= 0x100000) {
        if (times <= 0x10000) return int(std::uint32_t(value) * std::uint32_t(times)) / over;
        if (over <= times >> 4) return int(std::uint32_t(times / over) * std::uint32_t(value));
    } else if (over <= value >> 4) {
        return int(std::uint32_t(value / over) * std::uint32_t(times));
    }
    return int(std::int64_t(value) * times / over);
}
// A fine damage (256ths) through a resistance as FUN_0057bf80 does it: nothing
// at or under 0 (LAB_0057bf94) or at 100 % or more, untouched at 0 %, else
// MulDiv (FUN_00483360), which over 0x100000 divides first and multiplies in
// 32 bits; a wrapped (negative) result deals nothing.
inline int resisted_fine(int fine, int res) {
    if (fine <= 0 || res >= 100) return 0;
    if (res == 0) return fine;
    return std::max(fine > 0x100000 ? int(std::uint32_t(fine / 100) * std::uint32_t(100 - res)) : int(std::int64_t(fine) * (100 - res) / 100), 0);
}
// Energy Shield (FUN_005ca6c0, the state's absorb event 0xb like Bone Armor's): the pool of mana and the two numbers of the
// state, calc1 (the percent of a field it takes, at most 95 here) and calc2 (`per16`: 16ths of mana per point absorbed, at least 1).
struct EnergyShield {
    int pct = 0, per16 = 1;
    std::int64_t mana = 0;               // the wearer's mana in 256ths, drained as it absorbs
    std::int64_t absorbed = 0;           // what it took so far, in 256ths of damage
    // One record field (256ths): the percent of it, at most what the mana pays for (MulDiv(mana, 16, calc2)); the mana
    // paid is MulDiv(taken, calc2, 16), not below 0. Returns what it took. A field at or under 0 is left alone.
    int take(int field) {
        if (field <= 0 || pct <= 0) return 0;
        const int mana_in = int(std::clamp<std::int64_t>(mana, 0, 0x7fffffff));
        const int part = std::max(std::min(mul_div(field, pct, 100), mul_div(mana_in, 16, per16)), 0);
        mana = std::max<std::int64_t>(mana - mul_div(part, per16, 16), 0);
        absorbed += part;
        return part;
    }
};

// Energy Shield on the frame's blows. FUN_005ca6c0 runs in state event 0xb, before the resistance pass (event 0xb is raised
// on each record BEFORE FUN_0057c1e0 reduces it), on the record's fields in the order of its table 0x6e30e0: physical (+8),
// fire (+0x10), lightning (+0x1c), cold (+0x24), magic (+0x20); poison is not in it. So the percent and the mana limit work on the
// UNRESISTED field and the resistance, the flat magic reduction and the physical reduction work on what is left: a field
// the shield took from is put back through them: the flat reduction first, then the resistance, a field under 0 kept and summed (0057bf80). The mana is drained field by field,
// record by record in landing order. The physical field is taken from `raw` as absorb_blows leaves it (Bone Armor first).
// What a blow carries that no field accounts for (a record built without them: Corpse Explosion, a Fire Golem's burn) is taken
// from as it stands, as the shield used to take everything.
// ponytail: a state absorb registered later runs first in game.exe (FUN_005c0ad0); the armor-before-shield order of Fight::absorb
// is kept, and a Cyclone Armor cut is not put back through the resistance. Magic has no percent resistance on a monster blow here
// (monster_blow), so its field goes through the flat reduction only.
// A field the shield did not touch keeps monster_blow's value, which has the same order (reduction, resistance, a negative kept in the total).
inline void energy_shield_blows(std::span<StruckBlow> blows, EnergyShield& shield) {
    constexpr std::array<int, 4> kElementOrder{ 0, 1, 2, 4 };
    for (auto& blow : blows) {
        const std::int64_t whole = blow_total(blow);
        std::int64_t total = whole;   // the record total (+0x4c) in 256ths: a field under 0 stays in it, the total is clamped once at the end
        const std::int64_t physical_before = physical_now(blow);
        const int physical_fine = blow.raw >= 0 ? blow.raw : blow.physical * 256 + blow.frac;
        std::int64_t accounted = physical_before;   // what the fields hold of the blow
        if (const int cut = shield.take(physical_fine); cut > 0) {
            blow.raw = physical_fine - cut;
            const int fine = reduce_fine_signed(blow.raw, blow.pct, blow.flat);
            total += fine - physical_before;
            blow.physical = std::max(fine, 0) >> 8;
        }
        for (const int type : kElementOrder) {
            auto& part = blow.parts[std::size_t(type)];
            if (part.raw <= 0) continue;
            accounted += part_fine(part);
            const int field = part.raw * 256;
            const int cut = shield.take(field);
            if (cut <= 0) continue;
            // FUN_0057bf80: a field at or under 0 is stored as 0; else the flat magic reduction comes off FIRST (and may leave it under 0,
            // which is stored and summed), then the resistance on what is still above 0.
            int fine = field - cut;
            if (fine > 0) {
                fine -= blow.mdr * 256;
                if (fine > 0 && type != 4) fine = resisted_fine(fine, part.res);
            }
            total += fine - part_fine(part);
            if (type != 4) {
                blow.elemental = std::max(blow.elemental + (std::max(fine, 0) >> 8) - part.dealt, 0);
                if (blow.elemental <= 0 && !blow.poisoned) blow.marked = false;
            }
            part.dealt = std::max(fine, 0) >> 8;
            part.fine = fine;
        }
        if (const std::int64_t loose = whole - accounted; loose > 0) total -= shield.take(int(std::min<std::int64_t>(loose, 0x7fffffff)));
        store_total(blow, total);
    }
}

// Static Field's record on one unit (FUN_005c96a0), in 256ths before the
// resistance, or -1 when it makes none. Life is the unit's in whole points
// (stat 6 >> 8): nothing under 1, nothing at or under floor % of its max
// life (DifficultyLevels StaticFieldMin, expansion games only), else pct %
// of it (MulDiv) leaving at least 1 point, then at least `low` (calc2). A
// negative lightning resistance is taken back out first (MulDiv(dmg, 100,
// 100 - res)), so it adds nothing once FUN_0057bf80 applies it.
// life_fine and max_fine are the unit's 256ths (stat 6 at 0x5c96c2, max
// life FUN_00625d10 at 0x5c96e1, each >> 8 there); res is the unit's own
// stat 0x29 (FUN_00625480 on FUN_0056c8e0's stat), as game.exe reads it
// here: the attacker's pierce comes off only in FUN_0057c6c0, after.
inline int static_field_fine(int life_fine, int max_fine, int pct, int low, int floor_pct, int res) {
    const int life = life_fine >> 8;
    if (life < 1) return -1;
    if (floor_pct != 0 && life <= mul_div(max_fine >> 8, floor_pct, 100)) return -1;
    int dmg = mul_div(life, pct, 100);
    if (life - dmg < 1) dmg = life - 1;
    dmg = std::max(dmg << 8, low);
    if (res < 0) dmg = mul_div(dmg, 100, 100 - res);
    return dmg;
}

// What a skill's missile carries (FUN_0064b860, the missile's Skill set):
// the skill's physical and elemental damage (with synergies and the
// element's mastery) in 256ths, the element (Skill::etype order: fire,
// lightning, cold, poison, magic, stun) and its length in ticks, and the
// weapon's share (SrcDam as written, 128ths; 0 for spells).
// undead_pct / demon_pct: the missile's pSrvDmgFunc 5 (FUN_005ad2f0, Blessed
// Hammer), dParam1 / dParam2: against an undead (a demon) it adds that % of
// the rolled element (MulDiv, per class, both when a monster is both) to the
// element's field before the resistance.
struct MissileDamage { int phys_lo = 0, phys_hi = 0, etype = -1, elo = 0, ehi = 0, elen = 0, srcdam = 0, undead_pct = 0, demon_pct = 0; };

// The caster's Concentration on Blessed Hammer (FUN_006461d0, called by the
// do FUN_005d0040 at 0x5d011a): the damagepercent of the caster's state 0x2a
// (the aura) times the skill's Param1 over 8, truncated toward 0; the do then
// sets the missile's magic min and max to MulDiv(stat, 100 + influence, 100)
// when it is not 0. `percent` is the state's damagepercent stat, 0 with no
// such state.
inline int aura_influence(int percent, int param1) { return percent * param1 / 8; }
inline MissileDamage influenced(MissileDamage damage, int influence) {
    if (influence == 0) return damage;
    damage.elo = mul_div(damage.elo, 100 + influence, 100);
    damage.ehi = mul_div(damage.ehi, 100 + influence, 100);
    return damage;
}

// A burst rolls its record once and hands a copy to every unit it takes in
// (FUN_005a9a70 and FUN_005aa8b0 call FUN_005a8c70 once; each unit resists
// its own copy): the physical and the element roll in 256ths, -1 until the
// first unit's blow draws it.
struct MissileRoll { int phys = -1, element = -1; };

// A skill's missile striking `t` (on top of `b`, the weapon's share when
// it has one): the physical and the element, each rolled between its min
// and max and less the monster's resistance; the element's resistance
// less the attacker's pierce (333 fire, 334 lightning, 335 cold, 336
// poison) unless the monster is immune (100 or more), to -100 at the
// least. Cold chills for the length less the same resist whatever the
// damage came to (FUN_0057c6c0 hands record +0x30 to FUN_0057af80 with no
// test on the cold), poison runs its per-tick damage over it, stun stands it.
// The pierce test is FUN_0057be00's: a monster's resistance takes the
// attacker's pierce stat only while under 100. Stats 0x99 / 0x76
// (FUN_0057c140) aren't read: no MonProp, MonUMod or state row gives them
// to a monster.
inline Blow missile_blow(const MissileDamage& damage, const Target& target, const std::array<int, 4>& pierce, Rng& rng,
                         Blow blow = { .hit = true }, MissileRoll* shared = nullptr) {
    if (damage.phys_hi > 0) {
        // FUN_005a89a0 rolls the physical in 256ths and FUN_0057bf80 resists that fine
        // value, the life stat takes it as is: keep the part under a point in Blow::frac
        // as the element below does.
        const int roll = shared && shared->phys >= 0 ? shared->phys : rng.range(damage.phys_lo, damage.phys_hi);
        if (shared) shared->phys = roll;
        blow.phys += roll >> 8;
        // FUN_0057be00 floors a monster's resistance at -100 (no upper cap but the immunity at 100).
        const int fine = resisted_fine(roll, std::max(target.res[0], -100));
        blow.damage += fine >> 8;
        blow.frac += fine & 255;
        blow.phys_fine += fine;
    }
    if (damage.etype < 0 || damage.etype > 5 || damage.ehi <= 0) return blow;
    static constexpr int kRes[5] = { 2, 3, 4, 5, 1 };             // element -> Target::res index
    // FUN_005a8910: nothing unless both ends are over 0, then min + rand(max -
    // min) on the missile's seed: the max itself is never rolled.
    const int low = std::min(damage.elo, damage.ehi), high = std::max(damage.elo, damage.ehi);
    const int roll = shared && shared->element >= 0 ? shared->element : low > 0 ? low + rng(high - low) : 0;
    if (shared) shared->element = roll;
    // FUN_005ad2f0: after the roll, MulDiv(roll, dParam, 100) more for each class the monster is in.
    // Magic only (etype 4; the one row with pSrvDmgFunc 5 is the hammer): for poison the base is
    // the roll x 25 (0x5a8e1a), which this does not model. game.exe rolls the hit twice from the
    // missile's seed (the record, then FUN_005a8c70 inside this function); d2d draws from one
    // shared fight stream, so the distribution is the same.
    const int rolled = roll + (target.undead && damage.undead_pct > 0 ? mul_div(roll, damage.undead_pct, 100) : 0)
                            + (target.demon && damage.demon_pct > 0 ? mul_div(roll, damage.demon_pct, 100) : 0);
    if (damage.etype == 5) { blow.stun_ticks = std::max(blow.stun_ticks, std::min(damage.elen, 250)); return blow; }
    int res = target.res[std::size_t(kRes[damage.etype])];
    if (damage.etype < 4 && res < 100) res = std::max(res - pierce[std::size_t(damage.etype)], -100);
    if (damage.etype == 3) {
        blow.poison += resisted(int(std::int64_t(rolled) * std::max(damage.elen, 1) >> 8), res);
        blow.poison_ticks = std::max(blow.poison_ticks, std::max(damage.elen, 1));
        blow.elemental = blow.elemental || resisted_fine(rolled, res) > 0;   // the record's poison field (per tick, 256ths) is tested as the others are
        return blow;
    }
    // FUN_005a89a0 rolls the element in 256ths, the resistance applies to
    // that fine value (FUN_0057bf80) and the life stat (6) takes it as is
    // (0057c852): keep the part under one point in `frac`.
    const int fine = resisted_fine(rolled, res);
    const int resisted_damage = fine >> 8;
    blow.damage += resisted_damage;
    blow.elemental = blow.elemental || (fine > 0 && damage.etype != 4);   // the record's element is in 256ths (over 0)
    blow.frac += fine & 255;
    if (damage.etype == 2) blow.chill_ticks = std::max(blow.chill_ticks, resisted(damage.elen, res));
    return blow;
}

// A monster's freeze (FUN_0057b230): the length FUN_0057c1e0 left, which
// is cut by the cold resist less pierce as the chill length is (record
// +0x34, row 6 of the table at 0x732980, as row 5 is +0x30); nothing when
// that is 0 or less. A special monster (MonStats boss, FUN_0063e9f0;
// monster data +0x16 & 8, FUN_005a0180: champions, uniques, superuniques;
// a hireling, FUN_0063ee90) is chilled for that length instead
// (FUN_0057af80), never frozen. Any other is frozen only at a negative
// coldeffect (FUN_0057af30), for the length / MonsterFreezeDivisor
// (+0x14), and isn't chilled for it.
struct Freeze { int freeze_ticks = 0, chill_ticks = 0; };
inline Freeze monster_freeze(int length, const Target& target, int cold_pierce, bool special, int cold_effect, int divisor) {
    int res = target.res[4];
    if (res < 100) res = std::max(res - cold_pierce, -100);
    const int resisted_length = resisted(length, res);
    if (resisted_length <= 0) return {};
    if (special) return { .chill_ticks = resisted_length };
    if (cold_effect >= 0 || divisor == 0) return {};
    return { .freeze_ticks = resisted_length / divisor };
}

// What a skill adds to a blow (docs/research/re/skills.md, "Melee skills"):
// its attack-rating bonus % (toht) and enhanced damage % (calc1) joining
// the gear's percentages, damage added after the build (calc2), the share
// of weapon damage (SrcDam, 128ths), knockback; a kick's damage comes from
// the boots and the skill's own physical damage (in 256ths) instead of the
// weapon, and isn't doubled by critical / deadly strike (FUN_005d54b0).
struct Swing {
    int ar_pct = 0, ed_pct = 0, flat = 0, srcdam = 128;
    bool kick = false, knockback = false;
    int skill_lo = 0, skill_hi = 0;
    int stun_ticks = 0;                   // EType stun: the target stands this long (FUN_0057aae0)
    int hit_class = 0;                    // the skill's HitClass, OR-ed into the weapon's
    int conv_type = -1, conv_pct = 0;     // calc4 % of the physical becomes this element (Skill::etype order)
    int freeze_len = 0;                   // the record's freeze length (+0x34): Blades of Ice's third charge (charge_bonus)
    // Elemental damage as % of the physical rolled (Vengeance, FUN_005cfe10:
    // FUN_0057b420's physical x calc1 fire, calc2 cold, calc3 lightning).
    int fire_pct = 0, cold_pct = 0, ltng_pct = 0, cold_len = 0;
    // Smite (FUN_005ce9f0): the shield's damage, sure to hit (a block marks the blow but does not stop it), and the record
    // marked built (flags 2), so FUN_0057dbf0 skips FUN_0057b7d0: no crit,
    // gear elements, leech, crushing blow or open wounds.
    bool smite = false;
    bool missile = false;                 // a missile's blow: Crushing Blow's divisor doubled (event 6, FUN_005bffc0)
};

// The player's melee hit on `t` (hit chance, then the monster's block):
// critical or deadly strike doubles the physical damage; physical resistance cuts it;
// leech is the physical damage dealt x steal % x Drain %; crushing blow
// takes the life left / the target's divisor (less physical resistance).
inline Blow player_blow(const Fighter& fighter, const Target& target, int clvl, Rng& rng, const Swing& swing = {}) {
    Blow blow{ .hit_class = fighter.hit_class | swing.hit_class };
    const int attack_rating = std::max(int(std::int64_t(fighter.ar_base) * (100 + fighter.ar_pct + swing.ar_pct) / 100), 1);
    if (!swing.smite && rng(100) >= hit_chance(attack_rating, target.armor_class, clvl, target.level)) return blow;
    blow.prevent_heal = !swing.smite && fighter.prevent_heal;
    if (target.block > 0 && rng(100) < target.block) {
        blow.blocked = true;
        if (!swing.smite) return blow;      // FUN_005ce9f0 sets the hit bit again after the block check (0x5cebb0): Smite lands
    }
    blow.hit = true;
    std::int64_t low, high;                                  // 256ths
    if (swing.smite) {
        const std::int64_t smite_pct = std::max<std::int64_t>(fighter.smite_pct + swing.ed_pct, -90);
        low = ((std::int64_t(fighter.smite_lo) << 8) + swing.skill_lo) * (100 + smite_pct) / 100;
        high = ((std::int64_t(fighter.smite_hi) << 8) + swing.skill_hi) * (100 + smite_pct) / 100;
    } else if (swing.kick) {
        const std::int64_t kick_pct = fighter.kick_pct + swing.ed_pct;
        low = swing.skill_lo + std::int64_t(swing.skill_lo) * swing.ed_pct / 100 + (std::int64_t(fighter.kick_lo) << 8) * (100 + kick_pct) / 100;
        high = swing.skill_hi + std::int64_t(swing.skill_hi) * swing.ed_pct / 100 + (std::int64_t(fighter.kick_hi) << 8) * (100 + kick_pct) / 100;
    } else {
        // Against an undead monster FUN_0057b660 adds 50 with a Blunt weapon plus stat 122 to the
        // record's enhanced damage, only when that sum is over 0. FUN_0057b7d0 asks for it when the attacker is
        // a player unit or has alignment 2 (FUN_006259b0), so a merc or a Shadow is not excluded here.
        const int undead_bonus = target.undead ? std::max((fighter.blunt ? 50 : 0) + fighter.undead_pct, 0) : 0;
        const std::int64_t physical_pct = std::max<std::int64_t>(fighter.phys_pct + swing.ed_pct + undead_bonus, -90);
        low = fighter.phys_lo + fighter.phys_lo * physical_pct / 100;
        high = fighter.phys_hi + fighter.phys_hi * physical_pct / 100;
    }
    std::int64_t damage = high > low ? low + rng(int(high - low)) : low;
    const int rolled = int(damage >> 8);                       // before crit: what Vengeance's elements are a share of
    // The mastery crit, critical strike and deadly strike are separate
    // rolls in that order; any one doubles (FUN_0057b7d0). Not kicks.
    const auto roll = [&](int chance) { return chance > 0 && rng(100) < chance; };
    if (!swing.kick && !swing.smite && (roll(fighter.mastery_crit) || roll(fighter.critical) || roll(fighter.deadly))) { damage *= 2; blow.deadly = true; }
    damage = damage * swing.srcdam / 128;
    // A skill's own damage beside the weapon's SrcDam share (Blade Shield:
    // FUN_0056e170 then FUN_0057b7d0); kicks and Smite took theirs above.
    if (!swing.kick && !swing.smite && swing.skill_hi > 0)
        damage += swing.skill_hi > swing.skill_lo ? swing.skill_lo + rng(swing.skill_hi - swing.skill_lo) : swing.skill_lo;
    // Conversion, last in the build (FUN_0057b7d0, record +0x65 / +0x68):
    // pct % of the physical moves to the element, calc2's add comes after;
    // cold chills and poison runs at least 50 ticks, poison an eighth of
    // it a tick (record [10], 256ths).
    const std::int64_t conv = swing.conv_type >= 0 && swing.conv_type < 5 ? damage * std::clamp(swing.conv_pct, 0, 100) / 100 : 0;
    damage -= conv;
    // The physical stays in 256ths through the resistance (FUN_0057bf80 on the record's fine value) and
    // the life stat takes it as is (0057c852): the whole points go in `damage`, the part under a point in `frac`.
    const std::int64_t rolled_fine = std::max<std::int64_t>(damage, 0) + std::int64_t(swing.flat) * 256;
    blow.phys = int(rolled_fine >> 8);
    const int fine = resisted_fine(int(rolled_fine), target.res[0]);
    const int phys = fine >> 8;
    blow.frac = fine & 255;
    blow.phys_fine = fine;
    // The stun: the skill's, else the gear's stat 66 x the share (FUN_0057b7d0: [0x11] when 0).
    const int stun = swing.stun_ticks > 0 ? swing.stun_ticks : fighter.stun_len * swing.srcdam / 128;
    if (swing.smite) {
        blow.damage = phys;
        blow.knockback = swing.knockback;
        blow.stun_ticks = std::min(stun, 250);
        return blow;
    }
    blow.life = phys * fighter.life_steal * target.drain / 10000;
    blow.mana = phys * fighter.mana_steal * target.drain / 10000;
    static constexpr int kRes[5] = { 2, 3, 4, 5, 1 };             // element -> Target::res index
    int elem = conv > 0 && swing.conv_type != 3 ? resisted(int(conv >> 8), target.res[std::size_t(kRes[swing.conv_type])]) : 0;
    int marking = swing.conv_type != 4 ? elem : 0;                 // what FUN_0057ce30 sees: the record's fire / lightning / cold / poison, not magic (+0x20)
    elem += resisted(rolled * swing.fire_pct / 100, target.res[2]) + resisted(rolled * swing.ltng_pct / 100, target.res[3]);
    marking += resisted(rolled * swing.fire_pct / 100, target.res[2]) + resisted(rolled * swing.ltng_pct / 100, target.res[3]);
    if (const int cold = resisted(rolled * swing.cold_pct / 100, target.res[4]); cold > 0) { elem += cold; marking += cold; blow.chill_ticks = resisted(std::max(swing.cold_len, 1), target.res[4]); }
    for (int element = 0; element < 5; ++element) {
        const auto [elo, ehi] = fighter.elem[std::size_t(element)];
        if (ehi <= 0) continue;
        // The gear's cold puts its length in the record with any cold rolled
        // (FUN_0057b7d0: +0x30 when +0x24 > 0, before resistance), and
        // FUN_0057c6c0 chills on it untested: cold resisted under a point
        // still chills; so does a conversion to cold, at least 50 frames (case 4).
        const int element_roll = rng.range(elo, ehi);
        const int element_damage = resisted(element_roll, target.res[std::size_t(kRes[element])]);
        if (element == 3) { blow.poison = element_damage; blow.poison_ticks = std::max(fighter.poison_len, 1); continue; }
        if (element == 2 && element_roll > 0) blow.chill_ticks = std::max(blow.chill_ticks, resisted(fighter.cold_len, target.res[4]));   // the length less cold resist too (FUN_0057c1e0)
        elem += element_damage;
        if (element != 4) marking += element_damage;
    }
    if (conv > 0 && swing.conv_type == 3) {
        const int ticks = std::max(fighter.poison_len, 50);
        blow.poison += resisted(int(conv / 8 * ticks >> 8), target.res[5]);
        blow.poison_ticks = std::max(blow.poison_ticks, ticks);
    }
    if (conv > 0 && swing.conv_type == 2)
        blow.chill_ticks = std::max(blow.chill_ticks, resisted(std::max(fighter.elem[2].second > 0 ? fighter.cold_len : 0, 50), target.res[4]));
    // Crushing Blow (FUN_005bffc0): the life left after the hit / the
    // divisor, less the raw damage resist's share (100 at most, none at 0
    // or less: not resisted's).
    int crushing_blow = 0;
    if (fighter.crushing > 0 && rng(100) < fighter.crushing) {
        blow.crushing = true;
        crushing_blow = std::max(target.hit_points - phys - elem, 0) / (target.crush_divisor * (swing.missile ? 2 : 1));
        if (target.res[0] > 0) crushing_blow -= crushing_blow * std::min(target.res[0], 100) / 100;
    }
    blow.bleed = fighter.open_wounds > 0 && rng(100) < fighter.open_wounds;
    blow.knockback = fighter.knockback || swing.knockback;
    blow.stun_ticks = std::min(stun, 250);
    blow.damage = phys + elem + crushing_blow;
    blow.elemental = marking > 0 || blow.poison > 0;
    return blow;
}

// A monster's attack on the player (`motion`: its mode, standing, WL or RN;
// `missile`: a spike rather than a swing): hit chance against defense plus
// the vs-melee / vs-missile bonus, but a swing at a running player always
// lands (FUN_0057ec10: mode 3 skips FUN_0057d9b0); then the defender's rolls
// (FUN_0057dfb0 / FUN_0057dd60): block (a third while running, FUN_00622d00
// and mode not 2: walking keeps it whole), Weapon Block standing (two claws:
// weapon class HT2), then evade
// while moving, else dodge a swing / avoid a missile (`sure`: none of
// these, the record's hit bit set without FUN_0057ec10: DiabRun's
// ResultFlags 1, DiabCold's, a monster's Charge); then physical damage
// less damage-reduced % then flat (it can reach 0), and each elemental
// attack (at its chance) less resistance, fire / lightning / cold less
// magic damage reduction. Cold chills for El Dur ticks (stat 56, added in
// FUN_0057b7d0), see chill_length. Poison (stats 57/58 = El min/max x 10,
// 59 = Dur x 2, LAB_005a502b) is life per tick in 256ths less poison
// resist, for its ticks less poison length reduction (the resist table at
// 0x732980 via FUN_0057bf80: value x (100 - res) / 100).
// Chill on the player: FUN_0057c140 zeroes cold and freeze length for
// cannot be frozen (stat 153), else halves them for half freeze duration
// (118); the cold resist cuts them (FUN_0057c1e0). A player's freeze is
// chill (FUN_0057b230 -> FUN_0057af80: state 11, -50 velocitypercent,
// attackrate and other_animrate, at least 1 tick, only ever lengthened).
inline int chill_length(const Fighter& defender, int len) {
    if (defender.cannot_freeze) return 0;
    return resisted(defender.half_freeze ? len / 2 : len, defender.res[2]);
}
// `physical` / `elemental` (fire, lightning, cold): the parts of `damage`
// Bone Armor (22) and Cyclone Armor (25) absorb.
// `frac`: the 256ths of a point the physical carries beyond `damage` (game.exe keeps life in 256ths).
// `raw`: the physical before the reduction, in 256ths (what absorbs take from first, StruckBlow::raw).
// `element_raw` / `element_dealt`: per element type (0 fire, 1 lightning, 2 cold, 4 magic) the rolls summed and what they dealt, whole points.
struct Taken { bool hit = false, blocked = false, dodged = false; int damage = 0, poison = 0, poison_ticks = 0, chill_ticks = 0, physical = 0, elemental = 0, frac = 0, raw = -1;
             std::array<int, 5> element_raw{}, element_dealt{}, element_fine{};
             std::optional<int> sum; };   // element_fine: each field after the reduction in 256ths, under 0 kept; sum: the record total before the clamp; `damage` / `frac` are it clamped   // by element type: the rolls before the resistance and what they dealt after it (whole points)
enum class Motion : std::uint8_t { standing, walking, running };
inline Taken monster_blow(const Fighter& defender, int dlvl, Motion motion, const MonStats& attacker, bool second_attack, Rng& rng,
                          bool missile = false, bool sure = false, bool exact = false) {
    Taken taken;
    const bool moving = motion != Motion::standing, running = motion == Motion::running;
    if (!sure) {
        if (!(running && !missile) && !attacker.no_hit_roll && rng(100) >= hit_chance(attacker.to_hit, defender.defense + (missile ? defender.def_missile : defender.def_melee), attacker.level, dlvl))
            return taken;
        if (defender.block > 0 && rng(100) < (running ? defender.block / 3 : defender.block)) { taken.blocked = true; return taken; }
        if (!moving && defender.weapon_block > 0 && rng(100) < defender.weapon_block) { taken.blocked = true; return taken; }
        const int dodge = moving ? defender.evade : missile ? defender.avoid : defender.dodge;
        if (dodge > 0 && rng(100) < dodge) { taken.dodged = true; return taken; }
    }
    taken.hit = true;
    // FUN_0057b420: min << 8 + rand((max - min) << 8) on the attacker's seed, the top never rolled; FUN_0057c1e0
    // reduces that record in 256ths (reduce_fine) and the life stat takes it as is (0057c852). A missile's
    // FUN_005a89a0 draws on the missile's own seed with no clamp; here it shares the roll (unverified).
    // `exact`: a record the caller builds itself (Corpse Explosion FUN_005c4d10, Diablo's cold touch FUN_005cce80: min == max
    // straight into FUN_0057c6c0 with the reduce flag, V read 005c4dc1 / 005ccf3f): not FUN_0057b420's roll, so no 256ths
    // are added, no clamp and no draw; the reduction and the absorbs still apply (57c6c0 calls 57c1e0 at 57c7a0).
    int low = second_attack ? attacker.a2_min : attacker.a1_min, high = second_attack ? attacker.a2_max : attacker.a1_max;
    if (!missile && !exact) { low = std::max(low, 1); high = std::max(high, low + 1); }   // min at least 1, max at least min + 1: an empty range still draws
    taken.raw = exact ? low * 256 : low * 256 + rng((high - low) * 256);
    // FUN_0057bf80, per field of the record: the flat reduction (physical: stat 34 then the percent; the elements and magic: the magic damage
    // reduction) comes off FIRST, then the resistance on what is still above 0; a field left under 0 is stored and summed into the record total
    // (+0x4c), which is clamped once (0057c852). A type's rows are one field of the record.
    const int physical_fine = reduce_fine_signed(taken.raw, defender.dr_pct, defender.dr_flat);
    taken.physical = std::max(physical_fine, 0) >> 8;
    std::int64_t total = physical_fine;
    for (const auto& element : attacker.elements) {
        if (element.type < 0 || element.mode != (second_attack ? "A2" : "A1") || (element.pct < 100 && rng(100) >= element.pct)) continue;
        const int roll = rng.range(element.min, element.max);
        switch (element.type) {
            case 2: if (roll > 0) taken.chill_ticks = std::max(taken.chill_ticks, chill_length(defender, element.dur)); [[fallthrough]];
            case 0: case 1: case 4: if (roll > 0) taken.element_raw[std::size_t(element.type)] += roll; break;
            case 3: if (const int per = resisted(roll * 10, defender.res[3]); per >= taken.poison) { taken.poison = per; taken.poison_ticks = resisted(element.dur * 2, defender.plr); } break;
            default: break;   // types above 4 have no field in the record
        }
    }
    for (const int type : { 0, 1, 2, 4 }) {
        const int roll = taken.element_raw[std::size_t(type)];
        if (roll <= 0) continue;
        int fine = roll * 256 - defender.mdr * 256;
        if (fine > 0 && type != 4) fine = resisted_fine(fine, defender.res[std::size_t(type)]);
        taken.element_fine[std::size_t(type)] = fine;
        taken.element_dealt[std::size_t(type)] = std::max(fine, 0) >> 8;
        if (type != 4) taken.elemental += taken.element_dealt[std::size_t(type)];
        total += fine;
    }
    taken.sum = int(total);
    total = std::max(total, std::int64_t(0));
    taken.damage = int(total >> 8);
    taken.frac = int(total & 255);
    if (attacker.poison_max > 0)
        if (const int per = resisted(rng.range(attacker.poison_min, attacker.poison_max), defender.res[3]); per >= taken.poison) {
            taken.poison = per; taken.poison_ticks = resisted(attacker.poison_len, defender.plr);
        }
    return taken;
}


// A kill's experience by the level difference (FUN_0057e2f0), in 256ths:
// the character above the monster by 6..10+ 207 / 159 / 110 / 61 / 13
// (0x6e1668); the monster above by 6..10+ 225 / 174 / 92 / 38 / 5
// (0x6e1694), or clvl / mlvl once the character's level is 25 or more.
inline std::int64_t kill_exp(int exp, int clvl, int mlvl) {
    static constexpr std::array<int, 11> kCharacterAbove{ 256, 256, 256, 256, 256, 256, 207, 159, 110, 61, 13 };
    static constexpr std::array<int, 11> kMonsterAbove{ 256, 256, 256, 256, 256, 256, 225, 174, 92, 38, 5 };
    int factor = 0;
    if (mlvl <= clvl) factor = kCharacterAbove[std::size_t(std::min(clvl - mlvl, 10))];
    else if (clvl >= 25 && mlvl > 0) return scale(exp, clvl, mlvl);
    else factor = kMonsterAbove[std::size_t(std::min(mlvl - clvl, 10))];
    return factor == 256 ? exp : scale(exp, factor, 256);
}

// What a kill gives a unit of level `clvl` (FUN_0057e480): 1 for a kill
// worth none, 0 at `max_level` (Experience.txt MaxLvl); else kill_exp, then
// the level's ExpRatio (`ratios`, by level) >> `shift` (the MaxLvl row's
// ExpRatio, FUN_0057e390: (exp >> shift) x ratio where exp x ratio would
// overflow), then + item_addexperience % of that.
inline std::int64_t kill_award(int exp, int clvl, int mlvl, int add_pct, std::span<const int> ratios, int shift, int max_level) {
    if (exp < 1) return 1;
    if (clvl >= max_level) return 0;
    auto award = int(kill_exp(exp, clvl, mlvl));
    const int ratio = clvl >= 1 && std::size_t(clvl) < ratios.size() ? ratios[std::size_t(clvl)] : 0;
    if (award > 0 && shift >= 1 && shift <= 31)
        award = award > (0x7fffffff >> (((ratio >> shift) + shift) & 31)) ? (award >> shift) * ratio : award * ratio >> shift;
    if (add_pct != 0) award += scale(award, add_pct, 100);
    return award;
}

// A kill's experience shared by the party members who share it
// (FUN_0057e6c0), by their character levels: two or more make the total
// the kill's + (n - 1) x exp x 89 / 256 (32-bit, rounded toward zero), and
// each member's share is its level x (total / the levels' sum), the
// quotient a float (x87 FSTP) and the product truncated (_ftol). Each share
// then takes its member's level penalty (kill_exp). One member (or none):
// the kill's, unshared.
// The quotient (0x57e754: FILD, FIDIV, FSTP float) comes out the same
// through a double: an int over a level sum under 2^28 is never within
// 2^-53 of a float's halfway point without being on it, so rounding to 53
// bits first (or 64, or the 24 of a single-precision FPU) can't move it.
inline std::vector<std::int64_t> party_shares(int exp, std::span<const int> levels) {
    if (levels.size() < 2) return std::vector<std::int64_t>(levels.size(), exp);
    const auto grown = std::int32_t(std::uint32_t(levels.size() - 1) * std::uint32_t(exp) * 0x59u);
    const std::int32_t total = grown / 256 + exp;
    std::int64_t sum = 0;
    for (const int level : levels) sum += level;
    const float share = sum > 0 ? float(double(total) / double(sum)) : 0.f;
    std::vector<std::int64_t> out;
    for (const int level : levels) out.push_back(std::int64_t(double(level) * double(share)));
    return out;
}

// Adds experience; every level reached (exp_next[level] = experience for
// level + 1) gives StatPerLevel stat points, a skill point and the class's
// life/stamina/mana per level (quarter points, 8.8 fixed stats), and fills
// all three. Returns the levels gained.
inline int gain_exp(d2d::d2s::Stats& stats, std::int64_t exp, const std::vector<std::int64_t>& exp_next, const ClassGains& gains) {
    using namespace d2d::d2s;
    stats.values[kExp] += exp;
    int gained = 0;
    for (;;) {
        const auto lvl = stats.get(kLevel);
        if (lvl < 1 || std::size_t(lvl) >= exp_next.size() || exp_next[std::size_t(lvl)] <= 0
            || stats.values[kExp] < exp_next[std::size_t(lvl)] || lvl >= 99) break;
        ++stats.values[kLevel];
        ++gained;
        stats.values[kStatPts] += gains.stat_per_level;
        stats.values[kSkillPts] += 1;
        for (auto [cur, max, per_level] : { std::tuple{ kLife, kMaxLife, gains.life_per_level },
                                    std::tuple{ kStamina, kMaxStamina, gains.stamina_per_level },
                                    std::tuple{ kMana, kMaxMana, gains.mana_per_level } }) {
            stats.values[std::size_t(max)] += std::int64_t(per_level) * 64;
            stats.values[std::size_t(cur)] += std::int64_t(per_level) * 64;
        }
    }
    // Then full (FUN_00570880: FUN_00625d10 / 25d60 / 25db0): life only if
    // it's above 0, mana and stamina always.
    if (gained) {
        if (stats.values[kLife] > 0) stats.values[kLife] = stats.values[kMaxLife];
        stats.values[kMana] = stats.values[kMaxMana];
        stats.values[kStamina] = stats.values[kMaxStamina];
    }
    return gained;
}

// Stamina a running frame costs outside town (FUN_0057f240, 8.8 fixed):
// CharStats RunDrain x 2, x (armor.txt speed / 10 + 1) for the body armor
// worn, less item_staminadrainpct (stat 154) percent; at least 1.
inline int stamina_drain(int run_drain, int armor_speed, int slower_pct) {
    int drain = run_drain * 2 * (armor_speed / 10 + 1);
    drain -= slower_pct * drain / 100;
    return std::max(drain, 1);
}

// A frame's stamina regen (FUN_00580500): max >> 8 standing / town
// standing (modes 1 / 5), >> 9 walking (2, 6; walking only above 1.0),
// none running or otherwise unless staminarecoverybonus (stat 28) is 1000+;
// plus bonus percent.
inline std::int64_t stamina_regen(std::int64_t cur, std::int64_t max, int mode, int bonus) {
    int shift = 8;
    if (mode == 2 || mode == 6) {
        if (mode == 2 && cur < 256) return cur;
        shift = 9;
    } else if (mode != 1 && mode != 5 && bonus < 1000) return cur;
    if (cur >= max) return cur;
    std::int64_t add = max >> shift;
    if (bonus) add += add * bonus / 100;
    return std::min(max, cur + add);
}

}  // namespace d2d::rules
