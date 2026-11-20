// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of combat.hpp's functions; their notes are in the header.
#include "combat.hpp"

#include "monsters.hpp"
#include "montypes.hpp"
#include "rules.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace d2d::rules {

int hit_chance(int attack_rating, int def, int alvl, int dlvl) {
    std::int64_t attack = attack_rating, defense = def;
    if (defense < 0) { attack -= defense; defense = 0; }
    if (attack < 0) { defense -= attack; attack = 0; }
    const std::int64_t chance = attack + defense != 0 ? attack * 100 / (attack + defense) : 100;
    return int(std::clamp<std::int64_t>(chance * 2 * alvl / std::max(alvl + dlvl, 1), 5, 95));
}

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
Fighter make_fighter(const Tables& tables, const d2d::d2s::Item* weapon, const d2d::d2s::Item* shield,
                     const StatSum& sum, const StatSum& weapon_sum, const d2d::d2s::Stats& stats,
                     const ClassGains& gains, int defense, const std::array<int, 4>& res,
                     const d2d::d2s::Item* boots, bool thrown) {
    using namespace d2d::d2s;
    Fighter fighter;
    const auto stat = [&](int id) { return sum[std::size_t(id)]; };
    const auto weapon_stat = [&](int id) { return weapon_sum[std::size_t(id)]; };
    const std::int64_t clvl = std::max<std::int64_t>(stats.get(kLevel), 1), str = stats.get(kStr), dex = stats.get(kDex);
    const ItemBase* weapon_base = nullptr;
    if (weapon && item_usable(*weapon)) if (const auto found = tables.item_base.find(weapon->code); found != tables.item_base.end()) weapon_base = &found->second;   // a broken or 0x4000 one's stats are off (FUN_0055f850, FUN_0055dbc0)
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
        fighter.smite_lo = found->second.mindam + int(stat(kNormalDamage));      // FUN_0057b420 adds stat 111 to both ends (0x57b4c3..0x57b4d6)
        fighter.smite_hi = found->second.maxdam + int(stat(kNormalDamage));
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
    fighter.absorb_pct = { int(stat(kAbsorbFirePercent)), int(stat(kAbsorbLightningPercent)), int(stat(kAbsorbColdPercent)), 0, int(stat(kAbsorbMagicPercent)) };
    fighter.absorb_flat = { int(stat(kAbsorbFire)), int(stat(kAbsorbLightning)), int(stat(kAbsorbCold)), 0, int(stat(kAbsorbMagic)) };
    fighter.thorns = int(stat(kAttackerTakesDamage)); fighter.thorns_light = int(stat(kAttackerTakesLightningDamage)); fighter.thorns_pct = int(stat(kThornsPercent));
    fighter.life_regen = int(stat(kHitPointRegeneration)); fighter.mana_regen = int(stat(kManaRecoveryBonus));
    return fighter;
}

Fighter simple_fighter(int min, int max, int attack_rating, int defense) {
    Fighter fighter;
    fighter.min = min; fighter.max = std::max(max, min); fighter.attack_rating = fighter.ar_base = attack_rating; fighter.defense = defense;
    fighter.phys_lo = min << 8; fighter.phys_hi = fighter.max << 8;
    return fighter;
}

// The client's player tick (FUN_00463390) steps WL / RN / TW by it
// (FUN_00623e00).
int moving_anim_rate(bool run, int walk_percent) {
    return std::clamp((run ? 0x65 : 0xd5) * std::max(walk_percent, 25) / 100, 0, 0x7fff);
}

int attack_speed(int rate, int ias, int wsm, int sias) {
    const int eias = std::clamp(effective_speed(ias) - wsm + sias, -85, 75);
    return std::max(rate * (100 + eias) / 100, 1);
}

int frame_tick(int frame, int start, int speed) {
    return std::max(1, (256 * (frame - start) + speed - 1) / speed);
}

int attack_ticks(int frames, int rate, int ias, int wsm, int sias, int start) {
    return frame_tick(frames, start, attack_speed(rate, ias, wsm, sias));
}

int attack_action_tick(int action, int rate, int ias, int wsm, int sias, int start) {
    return frame_tick(action, start, attack_speed(rate, ias, wsm, sias));
}

int attack_start_frame(int player_class, std::string_view wclass) {
    if (player_class < 0 || std::size_t(player_class) >= 7) return 0;
    for (const auto& row : kAttackStart)
        if (row.wclass == wclass) return row.by_class[std::size_t(player_class)];
    return kAttackStart.front().by_class[std::size_t(player_class)];
}

int speed_frames(int base_frames, int speed_stat) {
    if (base_frames <= 0 || speed_stat <= 0) return std::max(base_frames, 0);
    const int scaled = 256 * effective_speed(speed_stat) / 100;
    return (base_frames * 256 + 256 + scaled - 1) / (256 + scaled);
}

int get_hit_divisor(int hit_class) {
    switch (hit_class) {
    case 2: case 6: case 10: case 11: return 8;
    case 4: case 8: return 32;
    case 5: return 64;
    default: return 16;
    }
}

bool get_hit(std::int64_t max_life, std::int64_t damage, int hit_class, bool marked, Rng& rng) {
    if (damage < 256) return false;
    const int divisor = marked ? 16 : get_hit_divisor(hit_class);
    if (damage < max_life / divisor) return false;
    if (damage < max_life / (divisor / 2) && rng(2) == 0) return false;
    if (damage < max_life / (divisor / 4) && rng(4) == 0) return false;
    return true;
}

// +0x54 is the missile's stat 327 damage_framerate, Missiles.txt DamageRate (FUN_0059fa30, copied
// by its hit FUN_005ad730): set only on the burners, so any other hit takes the flat whole.
int frame_rate_flat(int flat, int rate) {
    const int fine = flat * 256;
    return fine > 0 && rate > 0 ? mul_div(fine, rate, 0x400) : fine;
}

int resisted_fine(int fine, int res) {
    if (fine <= 0 || res >= 100) return 0;
    if (res == 0) return fine;
    return std::max(fine > 0x100000 ? int(std::uint32_t(fine / 100) * std::uint32_t(100 - res)) : int(std::int64_t(fine) * (100 - res) / 100), 0);
}

int reduce_fine_by(int fine, int percent, int flat_fine) {
    if (fine <= 0) return 0;
    fine -= flat_fine;
    if (fine > 0 && percent != 0) fine = mul_div(fine, 100 - std::clamp(percent, -100, 100), 100);
    return fine;
}

std::int64_t blow_absorbed(const StruckBlow& blow) {
    std::int64_t total = 0;
    for (const auto& part : blow.parts) total += part.took;
    return total;
}

std::int64_t physical_now(const StruckBlow& blow) {
    if (blow.raw < 0) return std::int64_t(blow.physical) * 256 + blow.frac;
    const std::int64_t fine = reduce_fine_signed(blow.raw, blow.pct, blow.flat);
    return blow.sum ? fine : std::max<std::int64_t>(fine, 0);
}

void store_total(StruckBlow& blow, std::int64_t total) {
    if (blow.sum || total < 0) blow.sum = int(total);
    total = std::max(total, std::int64_t(0));
    blow.damage = int(total >> 8);
    blow.frac = int(total & 255);
}

int cyclone_take(int& pool, int field) {
    if (field <= 0 || pool <= 0) return 0;
    const int cut = std::min(pool, field);
    pool -= cut;
    return cut;
}

// tools/emu/gear_absorb.py runs it on game.exe.
int absorbed_fine(int field, int pct, int flat) {
    if (pct > 0) field -= mul_div(field, std::min(pct, 40), 100);
    if (flat > 0) field -= std::min(flat << 8, field);
    return field;
}

int elemental_fine(int field, int mdr_fine, int res, bool resist, int abs_pct, int abs_flat) {
    if (field <= 0) return 0;
    field -= mdr_fine;
    if (field > 0 && resist) field = resisted_fine(field, res);
    return absorbed_fine(field, abs_pct, abs_flat);
}

int elemental_took(int field, int mdr_fine, int res, bool resist, int abs_pct, int abs_flat) {
    return elemental_fine(field, mdr_fine, res, resist) - elemental_fine(field, mdr_fine, res, resist, abs_pct, abs_flat);
}

// FUN_0057a980: nothing when the amount is 0 or less or the unit is dead, else life = min(life +
// amount, max life) (a life over the max is brought down to it); the total then comes off, and one
// that leaves under 0x100 is 0 (dead; the later blows then heal nothing). One record at a time, in
// landing order, as game.exe applies them: the cap is hit per blow, so two blows in one frame are
// not one summed heal.
std::int64_t absorb_heal(std::span<const StruckBlow> blows, std::int64_t life, std::int64_t max_life) {
    std::int64_t healed = 0;
    for (const auto& blow : blows) {
        if (life < 0x100) break;
        if (const std::int64_t took = blow_absorbed(blow); took > 0) {
            const std::int64_t gain = std::min(life + took, max_life) - life;
            life += gain;
            healed += gain;
        }
        if (const std::int64_t total = blow_total(blow); total > 0) {
            life -= total;
            if (life < 0x100) life = 0;
        }
    }
    return healed;
}

void settle_blow(StruckBlow& blow, std::int64_t old) {
    if (blow.raw < 0) return;
    const std::int64_t fine = reduce_fine_signed(blow.raw, blow.pct, blow.flat);
    store_total(blow, blow_total(blow) + fine - old);
    blow.physical = int(std::max<std::int64_t>(fine, 0) >> 8);
}

// A blow whose fire / lightning / cold went keeps its mark only by its poison
// (FUN_0057ce30 reads the fields after, in 256ths: under a point left of one still
// marks; tools/emu/hit_absorb.py). `pool` is in 256ths, as game.exe's stat is:
// Bone Armor subtracts it from the physical field BEFORE the defender's reduction
// (`raw`; a blow without one takes it from `physical` and `frac`), then the flat
// and percent reduction (reduce_fine) are put back on what is left, as game.exe
// does (absorb event 0xb at 0x57c2fc, the loop at 0x57c378). Its oracle for the
// pool in 256ths and the absorb before the reduction is
// tools/emu/monster_damage.py (sets absorb and both); tools/emu/hit_absorb.py runs
// a frame's records through either armor's pool and FUN_0057ce30's mark. Cyclone
// Armor works on each part's unresisted field (raw x 256) in 256ths and puts what
// is left back through the flat magic reduction and the resistance
// (elemental_fine); a blow with no parts (built by hand) loses whole points of its
// elemental total.
int absorb_blows(std::span<StruckBlow> blows, int& pool, bool elemental) {
    int taken = 0;
    for (auto& blow : blows) {
        if (elemental && std::ranges::any_of(std::array{ 0, 1, 2 }, [&](int type) { return blow.parts[std::size_t(type)].raw > 0; })) {
            std::int64_t total = blow_total(blow);
            for (const int type : { 0, 2, 1 }) {   // fire, cold, lightning
                auto& part = blow.parts[std::size_t(type)];
                const int field = part.raw * 256;
                const int cut = cyclone_take(pool, field);
                if (cut <= 0) continue;
                const int fine = elemental_fine(field - cut, blow.mdr * 256, part.res, true, part.abs_pct, part.abs_flat);
                part.took = elemental_took(field - cut, blow.mdr * 256, part.res, true, part.abs_pct, part.abs_flat);
                total += fine - part_fine(part);
                blow.elemental = std::max(blow.elemental + (std::max(fine, 0) >> 8) - part.dealt, 0);
                part.dealt = std::max(fine, 0) >> 8;
                part.fine = fine;
                taken += cut;
            }
            store_total(blow, total);
            // FUN_0057ce30 reads the fields in 256ths: a fire, lightning or cold field with under a point left still marks.
            const bool lit = std::ranges::any_of(std::array{ 0, 1, 2 }, [&](int type) { return part_fine(blow.parts[std::size_t(type)]) > 0; });
            if (!lit && !blow.poisoned) blow.marked = false;
            continue;
        }
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

bool struck_big(std::span<const StruckBlow> blows, std::int64_t max_life, Rng& seed) {
    bool big = false;
    for (const auto& blow : blows)
        if (blow.landed && get_hit(max_life, (std::int64_t(blow.damage) << 8) + blow.frac, blow.hit_class, blow.marked, seed)) big = true;
    return big;
}

// Its multiply and divide are unsigned (0x62435c..0x624368), so a negative
// factor wraps and normally clamps to 0x7fff. At v = -120 game.exe's IDIV
// divides by zero; d2d leaves that v unchanged.
int get_hit_rate(int speed, int fhr) {
    const int recovery = fhr != 0 && fhr != -120 ? 120 * fhr / (120 + fhr) : fhr;
    const std::uint32_t product = std::uint32_t(speed) * std::uint32_t(50 + recovery);
    return int(std::min(product / 100, std::uint32_t{ 0x7fff }));
}

// Sorceress SOGHHTH (8 frames at 256): 16 / 15 / 14 / 13 / 12 / 11 / 10 / 9
// / 8 / 7 / 6 ticks at FHR 0 / 5 / 9 / 14 / 20 / 30 / 42 / 60 / 86 / 142 /
// 280. The record's own event frames (AnimData +0x10, read in the same loop)
// queue only type-0 events, never the end, and in a get-hit those do
// nothing: a player's (event table 0x6e1810 -> FUN_005811d0, mode 4's action
// 0x732c10[4] = FUN_0057fe20) returns 1, a monster's (mode 3's action
// FUN_005a6de0) drops the unit's type-0 events.
int get_hit_ticks(int frames, int speed, int fhr) {
    const int rate = get_hit_rate(speed, fhr);
    if (rate <= 0) return 1;
    const int held = frames > 0 ? (frames * 256 - 1) / rate : 0;
    return std::max(held, 1) + 1;
}

int block_rate(int speed, int fbr, bool holy_shield) {
    return std::max(speed * (50 + effective_speed(fbr) + (holy_shield ? 50 : 0)) / 100, 1);
}

int block_ticks(int frames, int speed, int fbr, bool holy_shield) {
    const int held = frames > 0 ? (frames * 256 - 1) / block_rate(speed, fbr, holy_shield) : 0;
    return std::max(held, 1) + 1;
}

int get_hit_frame(int frames, int rate, int tick) {
    if (rate <= 0 || frames <= 0 || tick <= 0) return 0;
    const int held = (frames * 256 - 1) / rate;
    return std::min(tick, held) * rate / 256;
}

int player_get_hit_frame(int frames, int rate, int tick, int held) {
    return tick + 1 >= held ? -1 : get_hit_frame(frames, rate, tick);
}

int sequence_sias(bool use_attack_rate, std::string_view seqtrans) {
    return use_attack_rate && seqtrans != "SC" ? -kSequenceRatePenalty : 0;
}

int cast_speed(int rate, int fcr) {
    return std::clamp(rate * std::min(100 + effective_speed(fcr), 175) / 100, 0, 0x7fff);
}

// FUN_00463390 asks FUN_006217c0 after each step whether `current + speed >=
// frames x 256`, so the last step is never taken: ceil(256 x frames / speed) - 1
// ticks, at least one (tools/emu/cast_rates.py runs game.exe's code for it).
int cast_ticks(int frames, int rate, int fcr) {
    if (frames <= 0) return 0;
    const int speed = std::max(cast_speed(rate, fcr), 1);
    return std::max((256 * frames + speed - 1) / speed - 1, 1);
}

// FUN_00621260 sets the countdown (+0x48) to the sequence's step count x 256
// (0x62129a) and the mode ends once it is <= 0: ceil(256 x steps / speed), with no
// step skipped as a plain cast's is, at the cast speed (FUN_006216e0 says yes for
// a mode 0x12 whose skill's +0x12 is 10).
int cast_sequence_ticks(int steps, int rate, int fcr) {
    if (steps <= 0) return 0;
    const int speed = std::max(cast_speed(rate, fcr), 1);
    return (256 * steps + speed - 1) / speed;
}

int cast_action_ticks(int frame, int rate, int fcr) {
    return std::max(256 * frame + std::max(cast_speed(rate, fcr), 1) - 1, 0) / std::max(cast_speed(rate, fcr), 1);
}

int open_wounds_fine(int clvl, bool special) {
    const int level = std::max(clvl, 1);
    const int bracket = level <= 15 ? 9 * (level - 1) : level <= 30 ? 126 + 18 * (level - 15) : level <= 45 ? 396 + 27 * (level - 30)
                      : level <= 60 ? 801 + 36 * (level - 45) : 1341 + 45 * (level - 60);
    return special ? (bracket + 40) / 2 : bracket + 40;
}

// FUN_005ca6c0 runs in state event 0xb, before the resistance pass (event 0xb is raised on each
// record BEFORE FUN_0057c1e0 reduces it), on the record's fields in the order of its table
// 0x6e30e0: physical (+8), fire (+0x10), lightning (+0x1c), cold (+0x24), magic (+0x20); poison is
// not in it. So the percent and the mana limit work on the UNRESISTED field and the resistance, the
// flat magic reduction and the physical reduction work on what is left: a field the shield took
// from is put back through them: the flat reduction first, then the resistance, a field under 0
// kept and summed (0057bf80). What a blow carries that no field accounts for (a record built
// without them: Corpse Explosion, a Fire Golem's burn) is taken from as it stands, as the shield
// used to take everything. The state cast later runs first (FUN_005c0ad0: Fight::absorb). Magic has
// no percent resistance on a monster blow here (monster_blow), so its field goes through the flat
// reduction only. A field the shield did not touch keeps monster_blow's value, which has the same
// order (reduction, resistance, a negative kept in the total).
void energy_shield_blows(std::span<StruckBlow> blows, EnergyShield& shield) {
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
            const int fine = elemental_fine(field - cut, blow.mdr * 256, part.res, type != 4, part.abs_pct, part.abs_flat);
            part.took = elemental_took(field - cut, blow.mdr * 256, part.res, type != 4, part.abs_pct, part.abs_flat);
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

// A negative lightning resistance is taken back out first (MulDiv(dmg,
// 100, 100 - res)), so it adds nothing once FUN_0057bf80 applies it.
// life_fine and max_fine are the unit's 256ths (stat 6 at 0x5c96c2, max
// life FUN_00625d10 at 0x5c96e1, each >> 8 there); res is the unit's own
// stat 0x29 (FUN_00625480 on FUN_0056c8e0's stat), as game.exe reads it
// here: the attacker's pierce comes off only in FUN_0057c6c0, after.
int static_field_fine(int life_fine, int max_fine, int pct, int low, int floor_pct, int res) {
    const int life = life_fine >> 8;
    if (life < 1) return -1;
    if (floor_pct != 0 && life <= mul_div(max_fine >> 8, floor_pct, 100)) return -1;
    int dmg = mul_div(life, pct, 100);
    if (life - dmg < 1) dmg = life - 1;
    dmg = std::max(dmg << 8, low);
    if (res < 0) dmg = mul_div(dmg, 100, 100 - res);
    return dmg;
}

int corpse_life_fine(const Monsters& monsters, int type, int difficulty, int level) {
    const auto ranges = monster_ranges(monsters, type, difficulty, level);
    return (ranges.min_hp + ranges.max_hp) << 7;
}

int corpse_blast_fine(int life_fine, int low_pct, int high_pct, Rng& corpse_seed, int corpse_level, int caster_level) {
    const int low = mul_div(low_pct, life_fine, 100), high = mul_div(high_pct, life_fine, 100);
    int fine = corpse_seed(high - low) + low;
    if (corpse_level != 0 && caster_level < corpse_level) fine = mul_div(fine, caster_level, corpse_level);
    return fine;
}

std::pair<int, int> corpse_blast_split(int fine, int element_pct, bool elemental) {
    element_pct = std::min(std::max(element_pct, 0), 100);
    if (element_pct <= 0 || !elemental) return { fine, 0 };
    return { mul_div(fine, 100 - element_pct, 100), mul_div(fine, element_pct, 100) };
}

MissileDamage influenced(MissileDamage damage, int influence) {
    if (influence == 0) return damage;
    damage.elo = mul_div(damage.elo, 100 + influence, 100);
    damage.ehi = mul_div(damage.ehi, 100 + influence, 100);
    return damage;
}

Blow missile_blow(const MissileDamage& damage, const Target& target, const std::array<int, 4>& pierce, Rng& rng,
                  Blow blow, MissileRoll* shared) {
    if (damage.phys_hi > 0) {
        // FUN_005a89a0 rolls the physical in 256ths and FUN_0057bf80 resists that fine
        // value, the life stat takes it as is: keep the part under a point in Blow::frac
        // as the element below does.
        const int roll = shared && shared->phys >= 0 ? shared->phys : rng.range(damage.phys_lo, damage.phys_hi);
        if (shared) shared->phys = roll;
        blow.phys += roll >> 8;
        // FUN_0057be00 floors a monster's resistance at -100 (no upper cap but the immunity at 100).
        const int fine = resisted_fine(int(target.paired(roll)), std::max(target.res[0], -100));
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
    const int rolled = int(target.paired(roll + (target.undead && damage.undead_pct > 0 ? mul_div(roll, damage.undead_pct, 100) : 0)
                                              + (target.demon && damage.demon_pct > 0 ? mul_div(roll, damage.demon_pct, 100) : 0)));
    if (damage.etype == 5) { blow.stun_ticks = std::max(blow.stun_ticks, std::min(damage.elen, 250)); return blow; }
    int res = target.res[std::size_t(kRes[damage.etype])];
    if (damage.etype < 4 && res < 100) res -= pierce[std::size_t(damage.etype)];
    res = std::max(res, -100);   // FUN_0057be00 also floors magic, which has no pierce stat.
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

Freeze monster_freeze(int length, const Target& target, int cold_pierce, bool special, int cold_effect, int divisor) {
    int res = target.res[4];
    if (res < 100) res = std::max(res - cold_pierce, -100);
    const int resisted_length = resisted(length, res);
    if (resisted_length <= 0) return {};
    if (special) return { .chill_ticks = resisted_length };
    if (cold_effect >= 0 || divisor == 0) return {};
    return { .freeze_ticks = resisted_length / divisor };
}

int leech_fine(int physical_fine, const Target& target, int steal, int divisor) {
    if (target.drain <= 0 || steal == 0) return 0;
    const int life = target.life_fine >= 0 ? target.life_fine : target.hit_points * 256;
    const int physical = std::min(physical_fine, life);
    const int per = (steal << 6) / std::max(divisor, 1);
    if (per == 0 || physical <= 0) return 0;
    int amount = mul_div(physical, per, 100);
    if (target.drain != 100) amount = mul_div(amount, target.drain, 100);
    return amount / 64;
}

Blow player_blow(const Fighter& fighter, const Target& target, int clvl, Rng& rng, const Swing& swing) {
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
        // FUN_0057b420 makes a minimum below 1 (in 256ths) one point (0x57b4d8) and, when the maximum is not above the minimum, the minimum plus
        // one point (0x57b4ea..0x57b4f4), both before the percents.
        std::int64_t base_low = (std::int64_t(fighter.smite_lo) << 8) + swing.skill_lo;
        if (base_low < 1) base_low = 256;
        std::int64_t base_high = (std::int64_t(fighter.smite_hi) << 8) + swing.skill_hi;
        if (base_high <= base_low) base_high = base_low + 256;
        low = base_low * (100 + smite_pct) / 100;
        high = base_high * (100 + smite_pct) / 100;
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
    const std::int64_t rolled_fine = target.paired(std::max<std::int64_t>(damage, 0) + std::int64_t(swing.flat) * 256);
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
    static constexpr int kRes[5] = { 2, 3, 4, 5, 1 };             // element -> Target::res index
    const auto share = [&](int pct) { return int(target.paired(rolled * pct / 100)); };
    int elem = conv > 0 && swing.conv_type != 3 ? resisted(int(target.paired(conv) >> 8), target.res[std::size_t(kRes[swing.conv_type])]) : 0;
    int marking = swing.conv_type != 4 ? elem : 0;                 // what FUN_0057ce30 sees: the record's fire / lightning / cold / poison, not magic (+0x20)
    elem += resisted(share(swing.fire_pct), target.res[2]) + resisted(share(swing.ltng_pct), target.res[3]);
    marking += resisted(share(swing.fire_pct), target.res[2]) + resisted(share(swing.ltng_pct), target.res[3]);
    if (const int cold = resisted(share(swing.cold_pct), target.res[4]); cold > 0) { elem += cold; marking += cold; blow.chill_ticks = resisted(std::max(swing.cold_len, 1), target.res[4]); }
    for (int element = 0; element < 5; ++element) {
        const auto [elo, ehi] = fighter.elem[std::size_t(element)];
        if (ehi <= 0) continue;
        // The gear's cold puts its length in the record with any cold rolled
        // (FUN_0057b7d0: +0x30 when +0x24 > 0, before resistance), and
        // FUN_0057c6c0 chills on it untested: cold resisted under a point
        // still chills; so does a conversion to cold, at least 50 frames (case 4).
        const int element_roll = rng.range(elo, ehi);
        const int element_damage = resisted(int(target.paired(element_roll)), target.res[std::size_t(kRes[element])]);
        if (element == 3) { blow.poison = element_damage; blow.poison_ticks = std::max(fighter.poison_len, 1); continue; }
        if (element == 2 && element_roll > 0) blow.chill_ticks = std::max(blow.chill_ticks, resisted(fighter.cold_len, target.res[4]));   // the length less cold resist too (FUN_0057c1e0)
        elem += element_damage;
        if (element != 4) marking += element_damage;
    }
    if (conv > 0 && swing.conv_type == 3) {
        const int ticks = std::max(fighter.poison_len, 50);
        blow.poison += resisted(int(target.paired(conv / 8) * ticks >> 8), target.res[5]);
        blow.poison_ticks = std::max(blow.poison_ticks, ticks);
    }
    if (conv > 0 && swing.conv_type == 2)
        blow.chill_ticks = std::max(blow.chill_ticks, resisted(std::max(fighter.elem[2].second > 0 ? fighter.cold_len : 0, 50), target.res[4]));
    // Crushing Blow (FUN_005bffc0, run by the strike event 5 or 6 of FUN_0057c6c0, before LAB_0057c852 takes the hit's damage):
    // the life the target has BEFORE the hit, in 256ths, / the divisor, less the raw damage resist's share (100 at most, none
    // at 0 or less: not resisted's).
    int crushing_fine = 0;
    if (fighter.crushing > 0 && rng(100) < fighter.crushing) {
        blow.crushing = true;
        crushing_fine = (target.life_fine >= 0 ? target.life_fine : target.hit_points * 256) / (target.crush_divisor * (swing.missile ? 2 : 1));
        if (target.res[0] > 0) crushing_fine -= mul_div(crushing_fine, std::min(target.res[0], 100), 100);
    }
    // The leech cuts the physical to the life the crush left: FUN_0057c6c0 reads stat 6 at 0x57c7f8, after the strike events.
    Target after_crush = target;
    after_crush.life_fine = (target.life_fine >= 0 ? target.life_fine : target.hit_points * 256) - crushing_fine;
    blow.life = leech_fine(fine, after_crush, fighter.life_steal, target.life_steal_divisor);
    blow.mana = leech_fine(fine, after_crush, fighter.mana_steal, target.mana_steal_divisor);
    blow.bleed = fighter.open_wounds > 0 && rng(100) < fighter.open_wounds;
    if (blow.bleed) blow.bleed_fine = open_wounds_fine(clvl, target.special);
    blow.knockback = fighter.knockback || swing.knockback;
    blow.stun_ticks = std::min(stun, 250);
    const int fraction = blow.frac + crushing_fine;                // the physical's part under a point and the crush, in 256ths
    blow.damage = phys + elem + (fraction >> 8);
    blow.frac = fraction & 255;
    blow.crush_fine = crushing_fine;
    blow.elemental = marking > 0 || blow.poison > 0;
    return blow;
}

// Cold chills for El Dur ticks (stat 56, added in FUN_0057b7d0), see
// chill_length. Poison (stats 57/58 = El min/max x 10, 59 = Dur x 2,
// LAB_005a502b) is life per tick in 256ths less poison resist, for its
// ticks less poison length reduction (the resist table at 0x732980 via
// FUN_0057bf80: value x (100 - res) / 100). Chill on the player:
// FUN_0057c140 zeroes cold and freeze length for cannot be frozen (stat
// 153), else halves them for half freeze duration (118); the cold resist
// cuts them (FUN_0057c1e0). A player's freeze is chill (FUN_0057b230 ->
// FUN_0057af80: state 11, -50 velocitypercent, attackrate and
// other_animrate, at least 1 tick, only ever lengthened).
int chill_length(const Fighter& defender, int len) {
    if (defender.cannot_freeze) return 0;
    return resisted(defender.half_freeze ? len / 2 : len, defender.res[2]);
}

Taken monster_blow(const Fighter& defender, int dlvl, Motion motion, const MonStats& attacker, bool second_attack, Rng& rng,
                   bool missile, bool sure, bool exact) {
    Taken taken;
    const bool moving = motion != Motion::standing, running = motion == Motion::running;
    if (!sure) {
        if (!(running && !missile) && !attacker.no_hit_roll && rng(100) >= hit_chance(attacker.to_hit, defender.defense + (missile ? defender.def_missile : defender.def_melee), attacker.level, dlvl))
            return taken;
        if (!(missile && attacker.unblockable) && defender.block > 0 && rng(100) < (running ? defender.block / 3 : defender.block)) { taken.blocked = true; return taken; }
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
    // FUN_0057c060 (0x57c060), the attacker / defender pair's percent: a prime
    // evil (MonStats +0xc & 0x80) on a player's pet (unit +0xc4 bit 31), 200
    // a hireling, else 400. FUN_0057c1e0 takes each damage field above 0 to
    // v x pct / 100 (FUN_00483360) before the absorbs and reductions. Its
    // other pairs (a player or a hireling or pet on a player 17, hireling on
    // hireling 25) don't come up here. The table's +0x20 flag is set on the
    // poison damage row (8, +0x28) but not the poison length's (7, +0x2c).
    // A hireling on a MonStats boss takes the difficulty's DifficultyLevels
    // row +0x38, HireableBossDamagePercent (the context's +4: FUN_00611d30,
    // 0x57c206); here the attacker is always a monster, never a hireling:
    // the merc's own blows take it through Target::pair_pct (Fight::merc_target).
    const int pair_pct = attacker.prime_evil ? defender.prime_evil_pct : 100;
    const auto pair_scaled = [&](int fine) { return pair_pct != 100 && fine > 0 ? int(std::int64_t(fine) * pair_pct / 100) : fine; };
    taken.raw = pair_scaled(taken.raw);
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
            case 3: if (const int per = resisted(pair_scaled(roll * 10), defender.res[3]); per >= taken.poison) { taken.poison = per; taken.poison_ticks = resisted(element.dur * 2, defender.plr); } break;
            default: break;   // types above 4 have no field in the record
        }
    }
    for (std::size_t type = 0; type < attacker.enchants.size(); ++type)      // a boss's enchants and its gear's elements, after its elements
        if (const auto& enchant = attacker.enchants[type]; enchant && type != 3) {
            const int roll = rng.range(enchant->first, enchant->second);
            if (roll <= 0) continue;
            if (type == 2) taken.chill_ticks = std::max(taken.chill_ticks, chill_length(defender, attacker.enchant_len));
            taken.element_raw[type] += roll;
        }
    for (const int type : { 0, 1, 2, 4 }) {
        if (pair_pct != 100) taken.element_raw[std::size_t(type)] = pair_scaled(taken.element_raw[std::size_t(type)] * 256) >> 8;   // 200 / 400: whole points stay whole
        const int roll = taken.element_raw[std::size_t(type)];
        if (roll <= 0) continue;
        const int fine = elemental_fine(roll * 256, defender.mdr * 256, type != 4 ? defender.res[std::size_t(type)] : 0, type != 4,
                                        defender.absorb_pct[std::size_t(type)], defender.absorb_flat[std::size_t(type)]);
        taken.element_fine[std::size_t(type)] = fine;
        taken.element_took[std::size_t(type)] = elemental_took(roll * 256, defender.mdr * 256, type != 4 ? defender.res[std::size_t(type)] : 0, type != 4,
                                                               defender.absorb_pct[std::size_t(type)], defender.absorb_flat[std::size_t(type)]);
        taken.element_dealt[std::size_t(type)] = std::max(fine, 0) >> 8;
        if (type != 4) taken.elemental += taken.element_dealt[std::size_t(type)];
        total += fine;
    }
    taken.sum = int(total);
    total = std::max(total, std::int64_t(0));
    taken.damage = int(total >> 8);
    taken.frac = int(total & 255);
    if (attacker.poison_max > 0)
        if (const int per = resisted(pair_scaled(rng.range(attacker.poison_min, attacker.poison_max)), defender.res[3]); per >= taken.poison) {
            taken.poison = per; taken.poison_ticks = resisted(attacker.poison_len, defender.plr);
        }
    return taken;
}

std::int64_t kill_exp(int exp, int clvl, int mlvl) {
    static constexpr std::array<int, 11> kCharacterAbove{ 256, 256, 256, 256, 256, 256, 207, 159, 110, 61, 13 };
    static constexpr std::array<int, 11> kMonsterAbove{ 256, 256, 256, 256, 256, 256, 225, 174, 92, 38, 5 };
    int factor = 0;
    if (mlvl <= clvl) factor = kCharacterAbove[std::size_t(std::min(clvl - mlvl, 10))];
    else if (clvl >= 25 && mlvl > 0) return scale(exp, clvl, mlvl);
    else factor = kMonsterAbove[std::size_t(std::min(mlvl - clvl, 10))];
    return factor == 256 ? exp : scale(exp, factor, 256);
}

// Oracled bit-for-bit (game.exe's own ratio and shift, from Experience.txt
// through TXT_Experience_GetExpRatio, FUN_00613e60):
// tools/emu/xp_ratio.py.
std::int64_t exp_ratio_apply(std::int64_t award, int ratio, int shift) {
    if (award <= 0 || shift < 1 || shift > 31) return award;
    return award > (0x7fffffff >> (((ratio >> shift) + shift) & 31)) ? (award >> shift) * ratio : award * ratio >> shift;
}

std::int64_t kill_award(int exp, int clvl, int mlvl, int add_pct, std::span<const int> ratios, int shift, int max_level) {
    if (exp < 1) return 1;
    if (clvl >= max_level) return 0;
    auto award = int(exp_ratio_apply(kill_exp(exp, clvl, mlvl), clvl >= 1 && std::size_t(clvl) < ratios.size() ? ratios[std::size_t(clvl)] : 0, shift));
    if (add_pct != 0) award += scale(award, add_pct, 100);
    return award;
}

// The quotient (0x57e754: FILD, FIDIV, FSTP float) comes out the same
// through a double: an int over a level sum under 2^28 is never within
// 2^-53 of a float's halfway point without being on it, so rounding to 53
// bits first (or 64, or the 24 of a single-precision FPU) can't move it.
std::vector<std::int64_t> party_shares(int exp, std::span<const int> levels) {
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

int gain_exp(d2d::d2s::Stats& stats, std::int64_t exp, const std::vector<std::int64_t>& exp_next, const ClassGains& gains) {
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

int stamina_drain(int run_drain, int armor_speed, int slower_pct) {
    int drain = run_drain * 2 * (armor_speed / 10 + 1);
    drain -= slower_pct * drain / 100;
    return std::max(drain, 1);
}

std::int64_t stamina_regen(std::int64_t cur, std::int64_t max, int mode, int bonus) {
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

}
