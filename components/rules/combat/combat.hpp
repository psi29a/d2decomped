// SPDX-License-Identifier: GPL-3.0-or-later
// Combat: the player (or merc) as a Fighter built from gear and stats, one
// blow each way (hit chance, block and the defender's rolls, damage,
// resistances, crushing / deadly / critical strike, leech, poison, chill),
// swing speed breakpoints, experience and levels. Traced in game.exe where
// noted: docs/research/re/combat.md.
#pragma once

#include "monsters.hpp"
#include "points.hpp"
#include "rules.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace d2d::rules {

// Chance to hit in percent: negative defense adds to the attack rating (and vice versa); c = AR x 100 / (AR +
// DEF), then c x 2 x alvl / (alvl + dlvl), clamped to 5..95.
int hit_chance(int attack_rating, int def, int alvl, int dlvl);

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
    // The unit's stats 21 / 22 (23 / 24 with a two-hander): the weapon's with its own %, + the gear's flat.
    int stat_lo = 0, stat_hi = 0;
    int throw_lo = 0, throw_hi = 0;                 // stats 159 / 160 the same way: the weapon's minmisdam / maxmisdam
    // The unit's stats 18 / 17 beside the weapon's own (op 13 keeps those on the weapon), and the
    // weapon's own stat 111: the char panel's sum (FUN_004e86f0).
    int unit_min_pct = 0, unit_max_pct = 0, item_normal = 0;
    int ar_base = 1, ar_pct = 0;
    int ar_base_bare = 1;                           // ar_base with the weapon's own stats off (FUN_004e9040)
    int mastery_tohit = 0;                          // ar_pct's share from the weapon mastery (342, or 345 throwing)
    // Kicks (FUN_00646280): stat 137 + the boots' kick damage, and its % —
    // the boots' StrBonus / DexBonus on strength / dexterity, + 25, + 17.
    int kick_lo = 0, kick_hi = 0, kick_pct = 0;
    int smite_lo = 0, smite_hi = 0, smite_pct = 0;  // the shield's (Smite, FUN_005ce9f0 -> FUN_0057b420 on the shield)
    bool blunt = false;                             // the weapon is a Blunt type (ItemTypes Equiv chain: club, hammer, mace, scepter, wand, staff)
    int undead_pct = 0;                             // stat 122 (item_undeaddamage_percent) over all the gear (the game reads the unit's own stat)
    int demon_pct = 0;                              // stat 121 (item_demondamage_percent), the same way: against demons, melee or missile
    std::array<std::pair<int, int>, 5> elem{};      // added damage (poison: its total over poison_len)
    int cold_len = 0, poison_len = 0;               // ticks
    int stun_len = 0;                               // stat 66 (gear), ticks
    int crushing = 0, deadly = 0, critical = 0, open_wounds = 0;   // chances, %
    int item_slow = 0;                    // item_slow (stat 150): gear's event 19 (FUN_005c0310) slows the unit a landed hit strikes
    int mastery_crit = 0;                           // stat 344, the weapon's mastery (FUN_00645830)
    int weapon_block = 0;                           // stat 348, two claws only (FUN_0057dca0)
    bool knockback = false;
    bool prevent_heal = false;                      // stat 117 (item_preventheal)
    int hit_class = 1;                              // the weapon's (Weapons.txt hit class; 1 hand to hand), FUN_00623c20
    int attack_start = 0;                           // the frame an A1 swing starts on with this weapon (attack_start_frame)
    bool bow = false;                               // its weapon's wclass is bow: a do 8 / 10 / 12 keeps srvmissilea
    int life_steal = 0, mana_steal = 0;             // %
    int ias = 0, wsm = 0, frw = 0, fhr = 0, fbr = 0, fcr = 0;
    int sias = 0;           // stat 68 attackrate the states give (auras, skills): added raw, not diminished (FUN_00623f50, 0x6241e7)
    int velocity_pct = 0;   // stat 0x43 velocitypercent on top of the 100 (Vigor, Burst of Speed, ...)
    int defense = 0, block = 0;                     // block %, standing still
    int def_melee = 0, def_missile = 0;             // stats 33 / 32: extra defense vs each
    int dodge = 0, avoid = 0, evade = 0;            // stats 338 / 339 / 340, %
    int dr_pct = 0, dr_flat = 0, mdr = 0;
    // The gear's absorb by element type (0 fire, 1 lightning, 2 cold, 4 magic; stats 142..149): percent, and flat in whole points.
    std::array<int, 5> absorb_pct{}, absorb_flat{};
    std::array<int, 4> res{};                       // fire, lightning, cold, poison, %
    int plr = 0;                                    // poison length reduced, % (stat 110 + the difficulty's penalty, at most 75)
    bool half_freeze = false, cannot_freeze = false; // stats 118 / 153
    int thorns = 0, thorns_light = 0;               // attackers take (melee)
    int thorns_pct = 0;                             // stat 131: % of a melee hit's damage back (Thorns)
    int life_regen = 0, mana_regen = 0;             // hpregen; manarecoverybonus %
    int prime_evil_pct = 100;                       // FUN_0057c060's percent on a prime evil's blow: a hireling 200, a player's other pet 400
};

// Builds the Fighter from the worn weapon / shield, the summed item stats (`sum`.
Fighter make_fighter(const Tables& tables, const d2d::d2s::Item* weapon, const d2d::d2s::Item* shield,
                     const StatSum& sum, const StatSum& weapon_sum, const d2d::d2s::Stats& stats,
                     const ClassGains& gains, int defense, const std::array<int, 4>& res,
                     const d2d::d2s::Item* boots = nullptr, bool thrown = false);

// A fighter with just damage, attack rating and defense (the merc, its
// arrows, tests).
Fighter simple_fighter(int min, int max, int attack_rating, int defense = 0);

// Diminishing returns on the speed stats (FUN_00621740, table 0x6e8e24): IAS / FHR / FBR count
// 120 x v / (120 + v), FRW 150 x v / (150 + v), for any v but 0, a negative too (truncated toward zero).
// At v = -cap game.exe's IDIV divides by zero; d2d keeps v (deviations.md #13, as get_hit_rate).
inline int effective_speed(int speed, int cap = 120) { return speed != 0 && speed != -cap ? cap * speed / (cap + speed) : speed; }

// A player's walk / run / town-walk animation rate, 1/256 frames a tick.
int moving_anim_rate(bool run, int walk_percent);

// Ticks an attack animation of `frames` frames at AnimData `rate` takes with `ias` and weapon speed `wsm`
// (1.10).
// `ias` and weapon speed `wsm` (1.10): EIAS = effective IAS - WSM, clamped
int attack_speed(int rate, int ias, int wsm, int sias = 0);
// The tick animation frame `frame` is reached on, counted from the tick a mode starts, when it starts on frame
// `start` and plays `speed` / 256 frames a tick.
int frame_tick(int frame, int start, int speed);

// A frame-zero animation on FUN_00623e00's clock. Rate changes leave the
// accumulated position in place. An empty result marks the end of the mode.
struct FixedAnimationClock {
    std::uint32_t ticks = 0;
    std::uint64_t position = 0;

    constexpr auto advance(std::uint32_t elapsed_ms, std::uint32_t speed, std::uint32_t frames)
        -> std::optional<std::uint32_t> {
        const auto now = elapsed_ms / 40;
        if (now > ticks) {
            position += std::uint64_t(now - ticks) * speed;
            ticks = now;
        }
        if (position >= std::uint64_t(frames) * 256) return std::nullopt;
        return std::uint32_t(position);
    }
};

int attack_ticks(int frames, int rate, int ias, int wsm, int sias = 0, int start = 0);
// The tick a swing strikes on: its action frame (the AnimData flag byte), the same clock as attack_ticks.
int attack_action_tick(int action, int rate, int ias, int wsm, int sias = 0, int start = 0);
// The char panel's hover percentages: the chance to hit (FUN_004a7340: the player's attack rating against
// the monster's defence, the player's level first) and to be hit (FUN_004a74a0: the monster's rating
// against the player's defence, its level first), the inputs as the caller scaled them; held to 5..95.
int panel_hit_chance(int rating, int defense, int attacker_level, int defender_level);

// The frame a player's A1 or A2 swing starts on (FUN_00623b10, 0x623b88 .. 0x623bfa; every other mode and a monster start on frame 0):
// the weapon in hand (FUN_00623990) gives its class key (FUN_00629fe0: the weapons.txt wclass through the map at 0x7446a0), which picks
// a row of the table at 0x6e8e60, one dword a player class (d2s order). Only an Amazon and a Sorceress start in: frame 1 barehanded,
// frame 2 with a one-hand sword or thrusting weapon, a staff or a two-hand sword or thrusting weapon, 0 with a bow, crossbow or claws.
// A class key the map lacks gets row 0 (FUN_00629fe0, 0x62a040), the nothing-in-hand row: no vanilla weapon has one.
struct AttackStart { std::string_view wclass; std::array<int, 7> by_class; };
inline constexpr std::array<AttackStart, 9> kAttackStart = {{
    { "",    { 1, 1, 0, 0, 0, 0, 0 } },   // row 0: nothing in hand
    { "bow", { 0, 0, 0, 0, 0, 0, 0 } },   // row 1
    { "1hs", { 2, 2, 0, 0, 0, 0, 0 } },   // row 2
    { "1ht", { 2, 2, 0, 0, 0, 0, 0 } },   // row 3
    { "stf", { 2, 2, 0, 0, 0, 0, 0 } },   // row 4
    { "2hs", { 2, 2, 0, 0, 0, 0, 0 } },   // row 5
    { "2ht", { 2, 2, 0, 0, 0, 0, 0 } },   // row 6
    { "xbw", { 0, 0, 0, 0, 0, 0, 0 } },   // row 7
    { "ht1", { 0, 0, 0, 0, 0, 0, 0 } },   // row 12
}};
int attack_start_frame(int player_class, std::string_view wclass);

// Frame count after a speed stat (FCR / FHR / FBR) shortens `base_frames`: E = 120 v / (120 + v); shown =
// ceil(base x 256 / (256 + floor(256 E / 100))).
int speed_frames(int base_frames, int speed_stat);

// The get-hit test on a landed hit.
// `max_life` and `damage` in 256ths; `marked`: an element or a doubled hit.
int get_hit_divisor(int hit_class);
bool get_hit(std::int64_t max_life, std::int64_t damage, int hit_class, bool marked, Rng& rng);

// FUN_0057c1e0 (0x57c252..0x57c2a4): the flat (stat 34 or 35, whole points) in 256ths, scaled by the record's +0x54 / 1024
// (FUN_00483360) when that is above 0.
int frame_rate_flat(int flat, int rate);
// A fine damage (256ths) through a resistance as FUN_0057bf80 does it: nothing
// at or under 0 (LAB_0057bf94) or at 100 % or more, untouched at 0 %, else
// MulDiv (FUN_00483360), which over 0x100000 divides first and multiplies in
// 32 bits; a wrapped (negative) result deals nothing.
int resisted_fine(int fine, int res);
// FUN_0057bf80 on a damage record's physical field, in 256ths: the flat
// reduction (stat 34, whole points) comes off first, then the percent (stat 36)
// of what is left; a field already at 0 stays 0, the product through MulDiv
// (FUN_00483360: over 0x100000 it divides first). reduce_fine clamps a result under 0; game.exe stores it, sums it into the
// record total and only ignores a total not above 0 (0057c852): reduce_fine_signed keeps it.
// FUN_0057c1e0 scales the whole record by FUN_0057c060's pair percent
// first (monster_blow: a prime evil's on a pet). A burner's hit scales the
// flat first: frame_rate_flat. reduce_fine_by takes the flat in 256ths.
int reduce_fine_by(int fine, int percent, int flat_fine);
inline int reduce_fine_signed(int fine, int percent, int flat) { return reduce_fine_by(fine, percent, flat * 256); }
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
    // Its striker wears Iron Maiden: its physical goes back once the absorbs took theirs (Fight::hurt_side).
    bool reflect = false;
    // The fire / lightning / cold / magic parts before the defender's resistance (Taken::element_raw, by monster_blow's
    // element type: 0 fire, 1 lightning, 2 cold, 4 magic; 3 is poison, which has no record field an absorb reads): the rolls
    // summed (whole points), what the blow carries of them now (`dealt`, whole points) and the resistance they went through.
    // An Energy Shield works on these, as game.exe's absorb event runs before the resistance pass (energy_shield_blows).
    // `fine`: the field as the resistance pass stores it, in 256ths and possibly under 0 (kNoFine: not known, `dealt` x 256 stands).
    static constexpr int kNoFine = std::numeric_limits<int>::min();
    // abs_*: the gear's absorb (Fighter::absorb_pct / absorb_flat); took: what that absorb took off the field, in 256ths and signed (FUN_0057bf10 adds it to the record's +0x48)
    struct Part { int raw = 0, dealt = 0, res = 0, fine = kNoFine, abs_pct = 0, abs_flat = 0, took = 0; };
    std::array<Part, 5> parts{};
    int mdr = 0;                         // the flat magic damage reduction taken off each of them before the resistance
    // The record total (+0x4c) in 256ths BEFORE it is clamped, when monster_blow built the blow from its fields: `damage` and
    // `frac` hold it clamped at 0. Unset: damage x 256 + frac is the total (a blow built by hand or by another path).
    std::optional<int> sum;
    // Result flag 4: FUN_0057cee0 tests the get-hit only with it. A burner's record (pSrvDoFunc 5) has 1 alone:
    // FUN_005ad730 sets 4 for a row with GetHit (Missiles.txt +4 bit 0x40, FUN_00661b20), and the flames have none.
    bool landed = true;
    int striker = -1;                    // the Monster::id it came from, -1 none: the killer when it takes the life under a point
};
inline int part_fine(const StruckBlow::Part& part) { return part.fine != StruckBlow::kNoFine ? part.fine : part.dealt * 256; }
// The record's absorbed total (+0x48) in 256ths: the sum over the four fields of what FUN_0057bf10 took, signed (a field under 0 gives a negative share).
std::int64_t blow_absorbed(const StruckBlow& blow);
// The record total in 256ths, unclamped when known.
inline std::int64_t blow_total(const StruckBlow& blow) { return blow.sum ? *blow.sum : (std::int64_t(blow.damage) << 8) + blow.frac; }
// The physical field now, in 256ths: under 0 when the blow keeps its fields signed (`sum`), else as `damage` holds it (clamped).
std::int64_t physical_now(const StruckBlow& blow);
void store_total(StruckBlow& blow, std::int64_t total);

// FUN_005c8780, Cyclone Armor's pool on one field in 256ths (FUN_005c87a0 calls it on fire, cold, then lightning): only a field
// above 0; the pool takes all of it it can. Returns the cut.
int cyclone_take(int& pool, int field);
// The gear's absorb of one elemental or magic field.
int absorbed_fine(int field, int pct, int flat);
// FUN_0057bf80 on an elemental or magic field in 256ths after the absorb: one at or under 0 is stored as 0; else the flat magic
// reduction comes off FIRST (and may leave it under 0, which is stored and summed), then the resistance on what is still above 0
// (magic has none on a monster blow here: `resist` false).
int elemental_fine(int field, int mdr_fine, int res, bool resist, int abs_pct = 0, int abs_flat = 0);
// What the absorb took off that same field (elemental_fine without it less elemental_fine with it), in 256ths, signed.
int elemental_took(int field, int mdr_fine, int res, bool resist, int abs_pct, int abs_flat);
// The life the absorbed amounts give back, in 256ths: FUN_0057c6c0 hands the record's +0x48 to
// FUN_0057a980 (0x57c844) BEFORE it takes the record total (+0x4c) off the life
// (0x57c852..0x57c874). `life` is the life before the frame's blows.
std::int64_t absorb_heal(std::span<const StruckBlow> blows, std::int64_t life, std::int64_t max_life);

// Puts a blow's reduced physical (`physical`, and its share of the total) back in line with `raw`; `old` is the physical field it had
// (physical_now before `raw` changed).
void settle_blow(StruckBlow& blow, std::int64_t old);

// A state's absorb pool on the frame's blows.
int absorb_blows(std::span<StruckBlow> blows, int& pool, bool elemental);

// FUN_0057cb00 once per landed blow (FUN_0057c6c0 then FUN_0057cee0 per
// record): each blow rolls on what is left of it (the record's total in
// 256ths, as clamped: `damage` and `frac`), its own class and mark; a
// get-hit if any is.
bool struck_big(std::span<const StruckBlow> blows, std::int64_t max_life, Rng& seed);

// The get-hit's animation rate, 1/256 frames a tick.
int get_hit_rate(int speed, int fhr);

// How many server ticks the get-hit mode holds.
int get_hit_ticks(int frames, int speed, int fhr);

// The block animation's rate, 1/256 frames a tick.
int block_rate(int speed, int fbr, bool holy_shield);
int block_ticks(int frames, int speed, int fbr, bool holy_shield);

// The frame game.exe's client draws `tick` ticks into a get-hit (tick 0 the mode set).
int get_hit_frame(int frames, int rate, int tick);
// A player's.
int player_get_hit_frame(int frames, int rate, int tick, int held);

// A player's attack sequence (mode SQ, 0x12) runs 30 points slower than its attack rate: FUN_00623f50's attack branch takes 30 off the
// percent (0x624290..0x62429b: unit type 0, mode 0x12, sub esi, 0x1e) before the clamp to 15..175. The branch is the one FUN_00621580 picks for
// a skill whose UseAttackRate is set (record +6 against the mask at 0x6ce274); FUN_006216e0 sends a skill whose seqtrans is SC (Lightning,
// Chain Lightning) to the cast branch, which has no such term. tools/emu/seq_rate.py runs the function on every sequence record.
inline constexpr int kSequenceRatePenalty = 30;
int sequence_sias(bool use_attack_rate, std::string_view seqtrans);
// The row a sequence stands on as it reaches row `row`: +0x38 goes on by the
// rate (`speed`, 256ths of a row a frame) from 0, one step a frame
// (FUN_00623e00), and an event is read with +0x38 at the step's row
// (FUN_00621210), the first step at or past `row`.
constexpr int seq_step_row(int row, int speed) {
    if (speed <= 0) return row;
    const int steps = (row * 256 + speed - 1) / speed;
    return steps * speed / 256;
}
static_assert(seq_step_row(9, 256) == 9 && seq_step_row(14, 179) == 14 && seq_step_row(9, 512) == 10);

// A cast animation's speed, 1/256 frames a tick (FUN_00623f50's cast branch at
// 0x623fcb: FUN_00621740 turns stat 105 into the effective FCR through the row
// at 0x6e8e3c): AnimData speed x min(100 + effective FCR, 175) / 100, clamped
// to 0..0x7fff.
int cast_speed(int rate, int fcr);

// Client ticks a plain cast (a mode with no sequence) lasts.
int cast_ticks(int frames, int rate, int fcr);

// Client ticks a cast sequence lasts (anim SQ with seqtrans SC: Lightning, Chain
// Lightning).
int cast_sequence_ticks(int steps, int rate, int fcr);

// The tick of a cast mode's action frame, counted from its start.
int cast_action_ticks(int frame, int rate, int fcr);

// Open Wounds (item_openwounds, stat 135; its item event is FUN_005bfe60): a hit that rolls it puts state 0x3e on the struck
// monster for 200 frames with a negative life regeneration (stat 0x4a, 256ths a frame): a bracket of the attacker's level
// (FUN_005bfda0 over 9, 18, 27, 36, 45: 9 a level to 15, then 18 to 30, 27 to 45, 36 to 60, 45 above) plus 40, and half of
// that on a champion, a unique or a superunique (FUN_005a0180 over the monster's data word +0x16 & 12). tools/emu/open_wounds.py
// runs the handler on game.exe.
inline constexpr int kOpenWoundsFrames = 200;
int open_wounds_fine(int clvl, bool special);

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
    // The leech's two inputs from the game (FUN_0057c6c0 0x57c7f8, FUN_0057c420): the target's life in 256ths (the
    // physical is cut to it before the leech; -1: hit_points x 256) and the difficulty's LifeStealDivisor /
    // ManaStealDivisor (DifficultyLevels, 1 / 2 / 3), which divide a PLAYER's steal.
    int life_fine = -1;
    int life_steal_divisor = 1, mana_steal_divisor = 1;
    // FUN_0057c060's pair percent on each damage field above 0, before the
    // resistances (FUN_0057c1e0): a hireling's blow on a MonStats boss takes
    // DifficultyLevels HireableBossDamagePercent (row +0x38). 100 none.
    int pair_pct = 100;
    bool special = false;                           // a champion, unique or superunique (monster data +0x16 & 12, FUN_005a0180): Open Wounds takes half
    [[nodiscard]] std::int64_t paired(std::int64_t field) const { return field > 0 && pair_pct != 100 ? field * pair_pct / 100 : field; }
};
// What one player hit does: the instant damage (physical after resistance,
// fire / lightning / cold / magic, crushing blow), leeched life and mana,
// poison (total and ticks), chill ticks, and whether it bled, knocked back.
struct Blow {
    bool hit = false, blocked = false, crushing = false, deadly = false, bleed = false, knockback = false;
    int bleed_fine = 0;                  // Open Wounds' life regeneration if `bleed`, 256ths a frame, for kOpenWoundsFrames (open_wounds_fine)
    int slow = 0;                        // the striker's item_slow when it landed (gear's event 19, FUN_005c0310): Fight::land slows the target
    int damage = 0, poison = 0, poison_ticks = 0, chill_ticks = 0;
    int life = 0, mana = 0;              // leeched life and mana in 256ths (FUN_0057c420), what the attacker gains
    int phys = 0;                        // the physical rolled, before resistance (Dragon Tail's fire is a share of it)
    int phys_fine = 0;                   // the physical after resistance, in 256ths (the record's +8 field)
    int crush_fine = 0;                  // the Crushing Blow's share of `damage` and `frac`, 256ths: not in the record's total (+0x4c), so not in the get-hit's size
    int frac = 0;                        // 256ths of a point the physical (a blow's fine value after resistance) and a missile's element dealt beyond `damage`; may pass 255 (game.exe keeps life in 256ths)
    int stun_ticks = 0;
    int hit_class = 0;                   // what the struck unit's impact sound is (sound_ids::impact)
    bool elemental = false;              // fire, lightning, cold or poison got through (not magic): the get-hit divides by 16 (get_hit)
    bool prevent_heal = false;           // the to-hit roll passed with stat 117 (FUN_0057d9b0 -> FUN_0057d810), blocked or not
};
inline int resisted(int dmg, int res) { return res >= 100 ? 0 : dmg * (100 - res) / 100; }
// Energy Shield (FUN_005ca6c0, the state's absorb event 0xb like Bone Armor's): the pool of mana and the two numbers of the
// state, calc1 (the percent of a field it takes; the data caps it at 95) and calc2 (`per16`: 16ths of mana per point absorbed, at least 1).
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

// Energy Shield on the frame's blows.
void energy_shield_blows(std::span<StruckBlow> blows, EnergyShield& shield);

// Static Field's record on one unit, in 256ths before the resistance, or -1 when it makes none.
int static_field_fine(int life_fine, int max_fine, int pct, int low, int floor_pct, int res);

// Corpse Explosion's blow.
int corpse_life_fine(const Monsters& monsters, int type, int difficulty, int level);
int corpse_blast_fine(int life_fine, int low_pct, int high_pct, Rng& corpse_seed, int corpse_level, int caster_level);
// calc3 % (0..100) of that blow goes to the skill's EType when it has one, the rest stays physical: {physical,
// element}, 256ths.
std::pair<int, int> corpse_blast_split(int fine, int element_pct, bool elemental);
// The walk (FUN_0056b7e0) takes its aurafilter within (aurarange + 1) / 2
// subtiles of the corpse; the physical reaches only aurarange / 2
// (FUN_005c4d10's FUN_006492a0 against its square): {walk, reach}.
inline std::pair<int, int> corpse_blast_reach(int aura_range) { return { (aura_range + 1) / 2, aura_range / 2 }; }

// Life Tap: the striker's life after a hit on the cursed unit, 256ths.
std::int64_t life_tap_life(std::int64_t life_fine, std::int64_t max_fine, int damage_fine, int pct);
// Iron Maiden: the +8 of the hit back on the cursed unit, 256ths; none when it hits nothing.
[[nodiscard]] std::optional<int> iron_maiden_fine(bool monster, bool hireling, bool struck_player, int phys_fine, int calc1, int calc2, int calc3);

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
MissileDamage influenced(MissileDamage damage, int influence);

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
Blow missile_blow(const MissileDamage& damage, const Target& target, const std::array<int, 4>& pierce, Rng& rng,
                  Blow blow = { .hit = true }, MissileRoll* shared = nullptr);

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
Freeze monster_freeze(int length, const Target& target, int cold_pierce, bool special, int cold_effect, int divisor);

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
    int row_lo = 0, row_hi = 0;           // a missile row's own physical (256ths): in the roll with the weapon's, so a crit doubles it
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
    // A missile's blow: its record and hit path (missile_physical: FUN_0064b860 + FUN_005a89a0),
    // no SrcDam scale after the crit, and Crushing Blow's divisor doubled (event 6, FUN_005bffc0).
    bool missile = false;
};

// A player's leech of a blow, in 256ths.
int leech_fine(int physical_fine, const Target& target, int steal, int divisor);

// The player's melee hit on `t` (hit chance, then the monster's block):
// critical or deadly strike doubles the physical damage; physical resistance cuts it;
// leech is the physical damage dealt x steal % x Drain %; crushing blow
// takes the life left / the target's divisor (less physical resistance). `damage_rng`
// is the attacker's unit seed when its physical roll is separate from the fight rolls.
Blow player_blow(const Fighter& fighter, const Target& target, int clvl, Rng& rng, const Swing& swing = {}, Rng* damage_rng = nullptr);

// A melee blow's record enhanced damage % (FUN_0057b7d0's +0xc before FUN_0057b420): the swing's, + the demons' and
// the undead's against such a monster.
int melee_record_pct(const Fighter& fighter, const Target& target, const Swing& swing);
// A missile blow's physical in 256ths (FUN_0064b860's record, FUN_005a89a0's hit): the range the hit rolls
// (low + rand(high - low); both 0 for none), the record's +0x78 percent and the hit's, which scales the roll.
struct MissilePhysical { std::int64_t low = 0, high = 0; int record_pct = 0, hit_pct = 0; };
MissilePhysical missile_physical(const Fighter& fighter, const Target& target, const Swing& swing);
// FUN_005a89a0: a rolled physical plus its `pct` %, toward zero.
std::int64_t missile_scaled(std::int64_t roll, int pct);
// A SrcDamage missile's own physical moved from `damage` into `swing`'s record (FUN_0064b860): one roll with
// the weapon's, before missile_blow adds the element.
void row_into_record(MissileDamage& damage, Swing& swing);

// A monster's attack on the player (`motion`.
// `missile`: a spike rather than a swing): hit chance against defense plus
int chill_length(const Fighter& defender, int len);
// `physical` / `elemental` (fire, lightning, cold): the parts of `damage`
// Bone Armor (22) and Cyclone Armor (25) absorb.
// `frac`: the 256ths of a point the physical carries beyond `damage` (game.exe keeps life in 256ths).
// `raw`: the physical before the reduction, in 256ths (what absorbs take from first, StruckBlow::raw).
// `element_raw` / `element_dealt`: per element type (0 fire, 1 lightning, 2 cold, 4 magic) the rolls summed and what they dealt, whole points.
struct Taken { bool hit = false, blocked = false, dodged = false; int damage = 0, poison = 0, poison_ticks = 0, chill_ticks = 0, physical = 0, elemental = 0, frac = 0, raw = -1;
             int physical_fine = 0;   // the physical after the reduction, 256ths (signed): the record's +8 when no absorb took from it (Frozen Armor's gate, FUN_005cac50 0x5cac80)
             std::array<int, 5> element_raw{}, element_dealt{}, element_fine{}, element_took{};   // element_took: what the gear's absorb took off each field (256ths, signed)
             std::optional<int> sum; };   // element_fine: each field after the reduction in 256ths, under 0 kept; sum: the record total before the clamp; `damage` / `frac` are it clamped   // by element type: the rolls before the resistance and what they dealt after it (whole points)
enum class Motion : std::uint8_t { standing, walking, running };
Taken monster_blow(const Fighter& defender, int dlvl, Motion motion, const MonStats& attacker, bool second_attack, Rng& rng,
                   bool missile = false, bool sure = false, bool exact = false);


// A kill's experience by the level difference, in 256ths.
std::int64_t kill_exp(int exp, int clvl, int mlvl);

// A kill award's ExpRatio step.
// `shift` 1..31 or at 0 or less; otherwise (award >> shift) x ratio when
std::int64_t exp_ratio_apply(std::int64_t award, int ratio, int shift);

// What a kill gives a unit of level `clvl`.
std::int64_t kill_award(int exp, int clvl, int mlvl, int add_pct, std::span<const int> ratios, int shift, int max_level);

// A kill's experience shared by the party members who share it, by their character levels.
std::vector<std::int64_t> party_shares(int exp, std::span<const int> levels);

// Adds experience; every level reached (exp_next[level] = experience for
// level + 1) gives StatPerLevel stat points, a skill point and the class's
// life/stamina/mana per level (quarter points, 8.8 fixed stats), and fills
// all three. Returns the levels gained.
int gain_exp(d2d::d2s::Stats& stats, std::int64_t exp, const std::vector<std::int64_t>& exp_next, const ClassGains& gains);

// Stamina a running frame costs outside town.
int stamina_drain(int run_drain, int armor_speed, int slower_pct);

// A frame's stamina regen.
std::int64_t stamina_regen(std::int64_t cur, std::int64_t max, int mode, int bonus);

}  // namespace d2d::rules
