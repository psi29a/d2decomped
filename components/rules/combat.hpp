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
    std::array<std::pair<int, int>, 5> elem{};      // added damage (poison: its total over poison_len)
    int cold_len = 0, poison_len = 0;               // ticks
    int crushing = 0, deadly = 0, critical = 0, open_wounds = 0;   // chances, %
    int mastery_crit = 0;                           // stat 344, the weapon's mastery (FUN_00645830)
    int weapon_block = 0;                           // stat 348, two claws only (FUN_0057dca0)
    bool knockback = false;
    int life_steal = 0, mana_steal = 0;             // %
    int ias = 0, wsm = 0, frw = 0, fhr = 0, fbr = 0, fcr = 0;
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
                            const d2d::d2s::Item* boots = nullptr) {
    using namespace d2d::d2s;
    Fighter fighter;
    const auto stat = [&](int id) { return sum[std::size_t(id)]; };
    const auto weapon_stat = [&](int id) { return weapon_sum[std::size_t(id)]; };
    const std::int64_t clvl = std::max<std::int64_t>(stats.get(kLevel), 1), str = stats.get(kStr), dex = stats.get(kDex);
    const ItemBase* weapon_base = nullptr;
    if (weapon && !broken(*weapon)) if (const auto found = tables.item_base.find(weapon->code); found != tables.item_base.end()) weapon_base = &found->second;   // a broken one's stats are off (FUN_0055f850)
    // In 256ths, as game.exe keeps them.
    std::int64_t low = (weapon_base ? weapon_base->mindam * (100 + weapon_stat(kMinDamagePercent)) / 100 : 1) + stat(kMinDamage) + stat(kNormalDamage),
                 high = (weapon_base ? weapon_base->maxdam * (100 + weapon_stat(kMaxDamagePercent) + weapon_stat(kMaxDamagePercentPerLevel) * clvl / 8) / 100 : 2) + stat(kMaxDamage) + stat(kMaxDamagePerLevel) * clvl / 8 + stat(kNormalDamage);
    low = std::max<std::int64_t>(low, 1) << 8;
    high = std::max<std::int64_t>(high << 8, low + 256);
    const std::int64_t pct = std::max<std::int64_t>(stat(kDamagePercent) + stat(kPassiveMasteryMeleeDamage) + (weapon_base ? str * weapon_base->str_bonus / 100 + dex * weapon_base->dex_bonus / 100 : 0), -90);
    fighter.phys_lo = int(low); fighter.phys_hi = int(high); fighter.phys_pct = int(pct);
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
    fighter.crushing = int(stat(kCrushingBlow)); fighter.deadly = int(stat(kDeadlyStrike) + stat(kDeadlyStrikePerLevel) * clvl / 8); fighter.critical = int(stat(kPassiveCriticalStrike));
    fighter.mastery_crit = int(stat(kPassiveMasteryMeleeCritical)); fighter.weapon_block = int(stat(kPassiveWeaponBlock));
    fighter.open_wounds = int(stat(kOpenWounds));
    fighter.knockback = stat(kKnockback) > 0;
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

// Ticks an attack animation of `frames` frames at AnimData `rate` takes with
// `ias` and weapon speed `wsm` (1.10): EIAS = effective IAS - WSM, clamped
// to -85..75; ticks = ceil(256 x frames / (rate x (100 + EIAS) / 100)).
inline int attack_ticks(int frames, int rate, int ias, int wsm, int sias = 0) {
    const int eias = std::clamp(effective_speed(ias) - wsm + sias, -85, 75);
    const int speed = std::max(rate * (100 + eias) / 100, 1);
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

// Open wounds: bleeding per second for a character level (1.10's table in
// 256ths a tick, times 25 ticks), for 8 seconds.
inline int open_wounds_per_sec(int clvl) {
    const int value = clvl < 15 ? 9 * clvl + 31 : clvl < 31 ? 18 * clvl - 104 : clvl < 46 ? 27 * clvl - 374
                : clvl < 61 ? 36 * clvl - 779 : 45 * clvl - 1319;
    return std::max(value, 0) * 25 / 256;
}

// A monster being struck: its life now and max, defense, level, block
// chance (0 unless it can block), resistances (physical, magic, fire,
// lightning, cold, poison %), Drain (the % of leech that works on it).
struct Target {
    int hit_points = 1, max_hp = 1, armor_class = 0, level = 1, block = 0;
    std::array<int, 6> res{};
    int drain = 100;
};
// What one player hit does: the instant damage (physical after resistance,
// fire / lightning / cold / magic, crushing blow), leeched life and mana,
// poison (total and ticks), chill ticks, and whether it bled, knocked back.
struct Blow {
    bool hit = false, blocked = false, crushing = false, deadly = false, bleed = false, knockback = false;
    int damage = 0, life = 0, mana = 0, poison = 0, poison_ticks = 0, chill_ticks = 0;
    int phys = 0;                        // the physical rolled, before resistance (Dragon Tail's fire is a share of it)
    int stun_ticks = 0;
};
inline int resisted(int dmg, int res) { return res >= 100 ? 0 : dmg * (100 - res) / 100; }

// What a skill's missile carries (FUN_0064b860, the missile's Skill set):
// the skill's physical and elemental damage (with synergies and the
// element's mastery) in 256ths, the element (Skill::etype order: fire,
// lightning, cold, poison, magic, stun) and its length in ticks, and the
// weapon's share (SrcDam as written, 128ths; 0 for spells).
struct MissileDamage { int phys_lo = 0, phys_hi = 0, etype = -1, elo = 0, ehi = 0, elen = 0, srcdam = 0; };

// A skill's missile striking `t` (on top of `b`, the weapon's share when
// it has one): the physical and the element, each rolled between its min
// and max and less the monster's resistance; the element's resistance
// less the attacker's pierce (333 fire, 334 lightning, 335 cold, 336
// poison) unless the monster is immune (100 or more), to -100 at the
// least. Cold chills for the
// length less the same resist, poison runs its per-tick damage over it, stun stands it.
// ponytail: pierce against immunity is the published rule; its code in
// game.exe isn't traced.
inline Blow missile_blow(const MissileDamage& damage, const Target& target, const std::array<int, 4>& pierce, Rng& rng,
                         Blow blow = { .hit = true }) {
    if (damage.phys_hi > 0) {
        const int physical = rng.range(damage.phys_lo, damage.phys_hi) >> 8;
        blow.phys += physical;
        blow.damage += resisted(physical, target.res[0]);
    }
    if (damage.etype < 0 || damage.etype > 5 || damage.ehi <= 0) return blow;
    static constexpr int kRes[5] = { 2, 3, 4, 5, 1 };             // element -> Target::res index
    const int roll = rng.range(damage.elo, damage.ehi);
    if (damage.etype == 5) { blow.stun_ticks = std::max(blow.stun_ticks, std::min(damage.elen, 250)); return blow; }
    int res = target.res[std::size_t(kRes[damage.etype])];
    if (damage.etype < 4 && res < 100) res = std::max(res - pierce[std::size_t(damage.etype)], -100);
    if (damage.etype == 3) {
        blow.poison += resisted(int(std::int64_t(roll) * std::max(damage.elen, 1) >> 8), res);
        blow.poison_ticks = std::max(blow.poison_ticks, std::max(damage.elen, 1));
        return blow;
    }
    const int resisted_damage = resisted(roll >> 8, res);
    blow.damage += resisted_damage;
    if (damage.etype == 2 && resisted_damage > 0) blow.chill_ticks = std::max(blow.chill_ticks, resisted(damage.elen, res));
    return blow;
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
    int conv_type = -1, conv_pct = 0;     // calc4 % of the physical becomes this element (Skill::etype order)
    // Elemental damage as % of the physical rolled (Vengeance, FUN_005cfe10:
    // FUN_0057b420's physical x calc1 fire, calc2 cold, calc3 lightning).
    int fire_pct = 0, cold_pct = 0, ltng_pct = 0, cold_len = 0;
    // Smite (FUN_005ce9f0): the shield's damage, sure to hit, and the record
    // marked built (flags 2), so FUN_0057dbf0 skips FUN_0057b7d0: no crit,
    // gear elements, leech, crushing blow or open wounds.
    bool smite = false;
};

// The player's melee hit on `t` (hit chance, then the monster's block):
// critical or deadly strike doubles the physical damage; physical resistance cuts it;
// leech is the physical damage dealt x steal % x Drain %; crushing blow
// takes a quarter of its current life (less physical resistance).
// ponytail: crushing blow's boss / difficulty divisors aren't applied (its
// code isn't located in game.exe yet).
inline Blow player_blow(const Fighter& fighter, const Target& target, int clvl, Rng& rng, const Swing& swing = {}) {
    Blow blow;
    const int attack_rating = std::max(int(std::int64_t(fighter.ar_base) * (100 + fighter.ar_pct + swing.ar_pct) / 100), 1);
    if (!swing.smite && rng(100) >= hit_chance(attack_rating, target.armor_class, clvl, target.level)) return blow;
    if (target.block > 0 && rng(100) < target.block) { blow.blocked = true; return blow; }
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
        const std::int64_t physical_pct = std::max<std::int64_t>(fighter.phys_pct + swing.ed_pct, -90);
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
    int phys = int(std::max<std::int64_t>(damage >> 8, swing.kick || conv ? 0 : 1)) + swing.flat;
    blow.phys = phys;
    phys = resisted(phys, target.res[0]);
    if (swing.smite) {
        blow.damage = phys;
        blow.knockback = swing.knockback;
        blow.stun_ticks = std::min(swing.stun_ticks, 250);
        return blow;
    }
    blow.life = phys * fighter.life_steal * target.drain / 10000;
    blow.mana = phys * fighter.mana_steal * target.drain / 10000;
    static constexpr int kRes[5] = { 2, 3, 4, 5, 1 };             // element -> Target::res index
    int elem = conv > 0 && swing.conv_type != 3 ? resisted(int(conv >> 8), target.res[std::size_t(kRes[swing.conv_type])]) : 0;
    elem += resisted(rolled * swing.fire_pct / 100, target.res[2]) + resisted(rolled * swing.ltng_pct / 100, target.res[3]);
    if (const int cold = resisted(rolled * swing.cold_pct / 100, target.res[4]); cold > 0) { elem += cold; blow.chill_ticks = resisted(std::max(swing.cold_len, 1), target.res[4]); }
    for (int element = 0; element < 5; ++element) {
        const auto [elo, ehi] = fighter.elem[std::size_t(element)];
        if (ehi <= 0) continue;
        const int element_damage = resisted(rng.range(elo, ehi), target.res[std::size_t(kRes[element])]);
        if (element == 3) { blow.poison = element_damage; blow.poison_ticks = std::max(fighter.poison_len, 1); continue; }
        if (element == 2 && element_damage > 0) blow.chill_ticks = std::max(blow.chill_ticks, resisted(fighter.cold_len, target.res[4]));   // the length less cold resist too (FUN_0057c1e0)
        elem += element_damage;
    }
    if (conv > 0 && swing.conv_type == 3) {
        const int ticks = std::max(fighter.poison_len, 50);
        blow.poison += resisted(int(conv / 8 * ticks >> 8), target.res[5]);
        blow.poison_ticks = std::max(blow.poison_ticks, ticks);
    }
    if (conv > 0 && swing.conv_type == 2 && resisted(int(conv >> 8), target.res[4]) > 0)
        blow.chill_ticks = std::max(blow.chill_ticks, resisted(std::max(fighter.elem[2].second > 0 ? fighter.cold_len : 0, 50), target.res[4]));
    int crushing_blow = 0;
    if (fighter.crushing > 0 && rng(100) < fighter.crushing) { blow.crushing = true; crushing_blow = resisted(target.hit_points / 4, target.res[0]); }
    blow.bleed = fighter.open_wounds > 0 && rng(100) < fighter.open_wounds;
    blow.knockback = fighter.knockback || swing.knockback;
    blow.stun_ticks = std::min(swing.stun_ticks, 250);
    blow.damage = phys + elem + crushing_blow;
    return blow;
}

// A monster's attack on the player (`moving`: walking or running;
// `missile`: a spike rather than a swing): hit chance against defense plus
// the vs-melee / vs-missile bonus; then the defender's rolls
// (FUN_0057dfb0 / FUN_0057dd60): block (a third while moving), Weapon
// Block standing (two claws: weapon class HT2), then evade
// while moving, else dodge a swing / avoid a missile; then physical damage
// less damage-reduced % then flat (it can reach 0), and each elemental
// attack (at its chance) less resistance, fire / lightning / cold less
// magic damage reduction. Cold chills for El Dur ticks (stat 56, added in
// FUN_0057b7d0), see chill_length. Poison (stats 57/58 = El min/max x 10,
// 59 = Dur x 2, FUN_005a502b) is life per tick in 256ths less poison
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
struct Taken { bool hit = false, blocked = false, dodged = false; int damage = 0, poison = 0, poison_ticks = 0, chill_ticks = 0; };
inline Taken monster_blow(const Fighter& defender, int dlvl, bool moving, const MonStats& attacker, bool second_attack, Rng& rng,
                          bool missile = false) {
    Taken taken;
    if (rng(100) >= hit_chance(attacker.to_hit, defender.defense + (missile ? defender.def_missile : defender.def_melee), attacker.level, dlvl)) return taken;
    if (defender.block > 0 && rng(100) < (moving ? defender.block / 3 : defender.block)) { taken.blocked = true; return taken; }
    if (!moving && defender.weapon_block > 0 && rng(100) < defender.weapon_block) { taken.blocked = true; return taken; }
    const int dodge = moving ? defender.evade : missile ? defender.avoid : defender.dodge;
    if (dodge > 0 && rng(100) < dodge) { taken.dodged = true; return taken; }
    taken.hit = true;
    int phys = second_attack ? rng.range(attacker.a2_min, attacker.a2_max) : rng.range(attacker.a1_min, attacker.a1_max);
    phys = std::max(phys * (100 - defender.dr_pct) / 100 - defender.dr_flat, 0);
    taken.damage = phys;
    for (const auto& element : attacker.elements) {
        if (element.type < 0 || element.mode != (second_attack ? "A2" : "A1") || rng(100) >= element.pct) continue;
        const int roll = rng.range(element.min, element.max);
        switch (element.type) {
            case 2: taken.damage += std::max(resisted(roll, defender.res[2]) - defender.mdr, 0); if (roll > 0) taken.chill_ticks = std::max(taken.chill_ticks, chill_length(defender, element.dur)); break;
            case 3: if (const int per = resisted(roll * 10, defender.res[3]); per >= taken.poison) { taken.poison = per; taken.poison_ticks = resisted(element.dur * 2, defender.plr); } break;
            case 4: taken.damage += std::max(roll - defender.mdr, 0); break;
            default: taken.damage += std::max(resisted(roll, defender.res[std::size_t(element.type)]) - defender.mdr, 0); break;
        }
    }
    return taken;
}

// Experience for a kill: the monster's, less when the character outlevels
// it by more than 5 (81 / 62 / 43 / 24 % at 6..9 levels, 5 % from 10), or
// scaled by clvl / mlvl when the monster is more than 5 levels higher.
// ponytail: single player: the party share waits for a party; Experience.txt
// ExpRatio (past level 69) waits for later acts.
inline std::int64_t kill_exp(int exp, int clvl, int mlvl) {
    static constexpr int kPenalty[5] = { 81, 62, 43, 24, 5 };
    if (clvl > mlvl + 5) return std::int64_t(exp) * kPenalty[std::min(clvl - mlvl - 6, 4)] / 100;
    if (mlvl > clvl + 5) return std::int64_t(exp) * clvl / mlvl;
    return exp;
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
