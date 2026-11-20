// SPDX-License-Identifier: GPL-3.0-or-later
// Champions and uniques: MonUMod.txt, the mods a champion or unique rolls
// (FUN_005a0760 -> FUN_005a0500 / FUN_005a0600) and what they do to it
// (FUN_005a2120 -> the mod functions at 0x73c008), from game.exe 1.14d.
// docs/research/re/monsters.md "Champions and uniques".
#pragma once

#include "montypes.hpp"
#include "rules.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::rules {

// A MonUMod row (0x20 bytes in game.exe): enabled, champion-only, fPick
// constraint, excluded MonTypes, pick weights by difficulty.
struct UMod {
    int id = 0;
    bool enabled = false, champion = false;
    int fpick = 0;
    std::string exclude1, exclude2;
    std::array<int, 3> cpick{}, upick{};
};
// MonUMod.txt, plus its `constants` column (by row): 0 champion chance, 1..3
// minion +hp%, 4..6 champion +hp%, 7..9 unique +hp%, 10 champion +tohit%,
// 11 champion +dmg%, 12 minion +tohit%, 13 unique +tohit%, 14 minion +dmg%
// (strong), 15 unique +dmg% (strong), 16..21 minion elemental min / max %,
// 22..27 champion's, 28..33 unique's (each by difficulty).
struct UMods {
    std::vector<UMod> rows;
    std::array<int, 34> constants{};
};

// What a spawned monster is (MonsterData +0x16 flags: 4 champion, 8 unique
// counted, 0x10 minion) and its mods (+0x1c, up to 9).
enum class Boss : std::uint8_t { none, champion, unique, superunique, minion };
struct BossInfo {
    Boss kind = Boss::none;
    std::vector<int> mods;                      // MonUMod ids, in the order they were added
    int name_seed = 0;                          // rndname (FUN_005a0ce0): low 16 bits of a step of its seed
};

// Named MonUMod ids.
namespace umod {
inline constexpr int rndname = 1, hpmultiply = 2, light = 3, leveladd = 4, strong = 5, fast = 6, curse = 7, resist = 8,
                     fire = 9, poisondead = 10, durieldead = 11, bloodraven = 12, rage = 13, spcdamage = 14, partydead = 15, champion = 16, lightning = 17, cold = 18, scarab = 20, questcomplete = 22, poisonhit = 23, manahit = 25, teleport = 26, spectralhit = 27,
                     stoneskin = 28, multishot = 29, aura = 30, goboom = 31, firespike = 32, suicideminion = 33, ai_after_death = 34, shatter = 35, ghostly = 36, fanatic = 37, possessed = 38, berserk = 39,
                     lightningdeath = 42;
}

namespace unique_detail {

// FUN_005a03e0: can monster `t` take mod `u`? Enabled; not an excluded
// MonType; fPick 2 (multishot) not for melee or nomultishot monsters; fPick 1 (teleport)
// needs MonStats2 mA1 and fPick 3 (fast) mWL (FUN_0046c140 bits 4 / 2).
bool can_take(const UMod& mod, const MonType& type);

// Weighted pick among the rows that pass (FUN_005a0500 champion weights,
// FUN_005a0600 unique weights not already taken): rand(total), walk.
template <class Weight>
inline int pick(const UMods& umods, const MonType& type, Rng& seed, Weight&& weight) {
    std::vector<std::pair<int, int>> candidates;
    int total = 0;
    for (const auto& mod : umods.rows)
        if (const int mod_weight = weight(mod); mod_weight > 0 && can_take(mod, type)) { candidates.emplace_back(mod.id, mod_weight); total += mod_weight; }
    int roll = seed(total);
    for (const auto& [id, mod_weight] : candidates) { if (roll < mod_weight) return id; roll -= mod_weight; }
    return 0;
}

}  // namespace unique_detail

// FUN_005a0760 on a monster just made unique-capable: with `champions`,
// rand(100) < the champion chance makes it a champion with one champion
// mod (cpick); else a unique with 1 + difficulty unique mods (upick, no
// repeats, 8 at most). Rolls the monster's own seed.
BossInfo roll_boss(const UMods& umods, const MonType& type, int difficulty, bool champions, Rng& seed, bool name = true);

// A superunique's mods (FUN_005a49b0): SuperUniques Mod1..3 (24 skipped),
// then `difficulty` more unique mods (upick, none twice), at most 5 before.
std::vector<int> superunique_mods(const UMods& umods, const MonType& type, const std::vector<int>& fixed, int difficulty, Rng& seed);

// An act boss as it's made (FUN_005b1cf0 from FUN_005b2a00, by MonStats
// BaseId; an uber by its own row): its mods, each FUN_005a4850(mod, 1),
// whose FUN_005a0320 sets flag 8 (the gold name, the Demon / Undead label,
// TreasureClass3). No mod has an init in 0x73c008, so no unique stats.
// Blood Raven also gets FUN_00639db0(unit, 0x76, 1): state 118
// (corpse_noselect), not stat 0x76 (half freeze), so her freezes aren't
// halved; `half_freeze` keeps that call. Most also bind a quest
// (FUN_005436b0: Andariel 6, Blood Raven 2, Mephisto 0x14, Diablo 0x17,
// Izual 0x16, Nihlathak 0x22, Baal 0x24, the orb 0x13), which d2d keys off
// the kill (World::andariel_died, blood_raven_died). Travincal's compelling
// orb also gets unit flag 0x20000. Duriel, Radament, the Summoner and a few
// others bind a quest and take no mod.
// The Fire Golem's FUN_005a4850(0x1f, 0) (goboom) is its pet's death blast
// (Fight::golem_blast). A golem replaced by a new one is dropped, not killed,
// so it doesn't blow.
// The ancients (base 0x21c) take no mod here: FUN_005b1c50 gives each four
// magic items (monsters.md "The ancients' gear", ancient_gear).
struct ActBoss {
    std::vector<int> mods;
    bool half_freeze = false;
};
std::optional<ActBoss> act_boss(int monstats_row, int base);

// The ancients' gear (FUN_005b1c50, from FUN_005b1cf0 for base 0x21c): four
// items each from the table at 0x6e2c90 (8 bytes a row per class, rows 0x18
// apart: the code, then the BodyLocs row it's worn on), by MonStats row
// 0x21c..0x21e (Talic, Madawc, Korlic).
std::vector<std::pair<std::string, int>> ancient_gear(const Tables& tables, int monstats_row, int difficulty);

// The label under a boss's name (client FUN_004adea0, from FUN_00452580):
// only for uniques (flag 8) and minions (0x10). It starts with "Demon"
// (0x275e) or "Undead" (0x275d) by MonStats' flags (FUN_00454ad0), then
// each mod's string by MonUMod id (the table at 0x725188; 0 = none), in
// the order it has them, each after the joiner (a space: FUN_004ac870
// copies L" " into 0x7c0c58), until the line passes 480 px. A minion's is
// "Minion" (0xc95), after the leading word and 0xf9b.
inline constexpr std::array<std::uint16_t, 31> kUModLabel{
    0, 0, 0, 0, 0, 0xc85, 0xc86, 0xc87, 0xc88, 0xc89, 0, 0, 0, 0, 0, 0,
    0, 0xc8b, 0xc8a, 0, 0, 0, 0, 0, 0xc91, 0xc8c, 0xc8e, 0xc8d, 0xc8f, 0xc90, 0xc92 };
inline constexpr std::uint16_t kMinionLabel = 0xc95, kMinionSpace = 0xf9b, kDemonLabel = 0x275e, kUndeadLabel = 0x275d;

// The colour of a boss's name over its life bar (client FUN_00454ad0, the
// font colour FUN_005022f0 takes): 4 gold for flag 8 (uniques, superuniques;
// a champion has it too), 3 blue for a champion (flag 4) over that, else 0
// white. MonStats rows 0xd3, 0xe5, 0xf2, 0xf3, 0x14d, 0x220, 0x23a and
// 0x2c0..0x2c5 (by MonStats Id below) are always gold; Andariel isn't one.
inline constexpr int kNameWhite = 0, kNameBlue = 3, kNameGold = 4;
int bar_name_colour(Boss kind, std::string_view monstats_id);

// The hover bar's immunity lines (client FUN_004526c0, the last call of
// FUN_00454ad0): over the six rows at 0x6d6418 (`kImmunities`, in
// ResDm..ResPo order), each resist over 99 adds its string as a line, in
// its font colour. None for MonStats rows 0x2c0..0x2c5, the six Ubers
// (unit +4, by MonStats Id here). tools/emu/immunity_table.py.
struct Immunity { int stat; std::uint16_t string; int colour; };
inline constexpr std::array<Immunity, 6> kImmunities{ { { 36, 0x2765, 4 }, { 37, 0x2766, 8 }, { 39, 0x2761, 1 },
                                                        { 41, 0x2763, 9 }, { 43, 0x2762, 3 }, { 45, 0x2764, 2 } } };
std::vector<Immunity> immunity_lines(std::string_view monstats_id, const std::array<int, 6>& resists);

// The MonSounds row a monster plays (client FUN_004ca410, UnitSnd.cpp): a
// superunique's own SuperUniques MonSound (record +0x18) when set (the
// Countess's, the Smith's); else, for flag 8 or 0x10 (champions, uniques,
// superuniques, minions), MonStats UMonSound (+0x16) when set (zombies:
// zombieunique); else MonStats MonSound (+0x14).
const std::string& boss_sound(const MonType& type, Boss kind, const std::string& superunique_sound);

// A champion's name (client FUN_004ac870, run through the mod table at
// 0x724d78 for the fixed mods 1..4, then the monster's own; the last one
// wins): "%0 %1" (0x2b40) with the word for its champion mod from 0x6da488
// (16 "Champion" 0xc94, 36 Ghostly .. 39 Berserker 0x2b4c..0x2b4f) and its
// name. The search checks the first four keys; no match gives the fifth.
inline constexpr std::uint16_t kChampionFormat = 0x2b40;
inline constexpr std::array<std::pair<int, std::uint16_t>, 5> kWord{ { { 16, std::uint16_t(0xc94) }, { 36, std::uint16_t(0x2b4c) }, { 37, std::uint16_t(0x2b4d) },
                                                                          { 38, std::uint16_t(0x2b4e) }, { 39, std::uint16_t(0x2b4f) } } };
std::uint16_t champion_word(const std::vector<int>& mods);

// What the mods do to a monster's stats (FUN_005a2120: the fixed rndname,
// hpmultiply, light, leveladd for uniques, then its own mods; champions
// and minions get theirs through `champion`). `boss` is the unique's /
// champion's own kind; a minion takes the minion rows.
struct BossStats {
    int level_add = 0;                          // leveladd +3 (a minion too); champion -1 on top (net +2)
    int exp_mult = 1;                           // unique and minion x5, champion x3
    int hp_pct = 0;                             // + life %
    int dmg_pct = 0, tohit_pct = 0;             // + damage %, + attack rating %
    int velocity_pct = 0;                       // + speed % (fast; champion +20)
    std::array<int, 6> res_add{};               // + DR%, MR%, fire, light, cold, poison resist (ResDm..ResPo order)
    bool double_defense = false;                // stone skin
    // Enchanted: the MonLvl damage % (min, max) each element adds, fire, lightning, cold: each init sets its own
    // stats (FUN_005a1990 0x30 / 0x31, FUN_005a1b00 0x32 / 0x33, FUN_005a1c70 0x36 / 0x37), so they stack.
    std::array<std::optional<std::pair<int, int>>, 3> elem_pct{};
    int elem_len = 0;                           // the cold's length (frames, stat 0x38): ghostly 150, Cold Enchanted 100 + 5 a level
    int elem_len_per_level = 0;                 // Cold Enchanted: FUN_005a1c70 adds stat 12 x 5 + 100
    int life_after_pct = 0;                     // possessed +100, berserker -75: after the hp% rows (FUN_005a0d20)
    int defense_pct = 0;                        // fanatic: item_armor_percent -70
    int phys_resist = -1;                       // ghostly: damageresist set to 80
};
// The cold length a boss's blow carries (frames): FUN_005a1c70 adds the unit's level x 5 + 100 (0x5a1dcd), the level taken as at least 1 and at
// most MonLvl's last row (0x5a1cf5..0x5a1d1c: max_level, the table's rows - 1); ghostly's FUN_005a1080 adds 150.
inline int boss_elem_length(const BossStats& stats, int level, int max_level) { return stats.elem_len + stats.elem_len_per_level * std::clamp(level, 1, max_level); }
// FUN_005a1370's "at most two immunities" counts the monster's resists as
// they stand: its MonStats ones plus the mods' adds so far (no 1.14d MonProp
// row adds a resist: monsters.md "MonProp"). Stone skin's defence x 2 comes
// before that gate, its damage reduction after it.
// DifficultyLevels ChampionDamageBonus / UniqueDamageBonus (+0x34 / +0x30,
// 90 / 75 / 66): the share of the champion and strong damage and to-hit
// bonuses a difficulty keeps (FUN_005a0e80, FUN_005a17e0).
inline constexpr std::array<int, 3> kBossBonus{ 90, 75, 66 };
BossStats boss_stats(const UMods& umods, const MonType& type, Boss kind, const std::vector<int>& mods, int difficulty);

// What a unique's mods do in the fight (the event hooks at 0x73c0b8, six
// a mod, run by FUN_005a4270; docs/research/re/monsters.md "Boss mods in
// the fight"). Only for the boss itself (flag 8), not its minions.
std::vector<int> family_mods(int base_id);

// What MonUMod 14 (spcdamage) sets on a trap monster (FUN_005a3b50, hook 0):
// its level (stat 12) = the area's monster level (FUN_0061dca0 through
// FUN_0061a1b0), at least 1, or 2 with no room; to-hit (stat 0x13) = level +
// 50, at most 90; then, by BaseId, the (stat, value) pairs it puts on the
// unit's stat list 1 (d2d_stat_list_set), in game.exe's order. d2d takes the
// level and to-hit for trap-melee (0x162, make_monster); the pairs are for the trap classes d2d
// makes no monster of (chest traps are objects, objects.md).
struct SpcDamage {
    int level = 2;
    int to_hit = 52;
    std::vector<std::pair<int, int>> list;
};
SpcDamage spcdamage(int base_id, std::optional<int> area_level);

// MonUMod 34 (ai_after_death, every reanimatedhorde1, BaseId 0x1b4) hook 1,
// FUN_005a3840 (after a mode change): into mode 0 (death) of an evil unit
// (alignment 0, FUN_006259b0), it drops the unit's queued event 7
// (FUN_00540e60) and rolls rand(100) on its seed under MonStats aip8
// (+0x80, by difficulty): then event 7 comes aip1 (+0x56) x 10 + 1 frames on.
struct AfterDeathQueue {
    bool cleared = false;                // event 7 dropped (the hook ran)
    std::optional<int> frames;           // event 7 due this many frames on
};
AfterDeathQueue ai_after_death_queue(int mode, int alignment, int chance, int aip1, Rng& rng);

// Hook 2, FUN_005a3910 (event 7 comes due): what the unit does. It first
// drops the unit's event 2 (its think). Then, in this order: not dead
// (FUN_005541b0: mode 0 or 0xc), a hidden corpse (FUN_0063a770) or a unique
// (monster data +0x16 & 8) or a good unit (alignment 2) does nothing; its
// spot taken (FUN_0064d870, pattern 1, mask 0x3c01) tries again aip1 x 10 + 1
// frames on; with the AI scratch (+0x14) at 2 or more, or not a corpse to
// raise (FUN_00645510: mode 0xc, not hidden, MonStats2 corpseSel, MonStats
// Velocity) or not evil, nothing; else skill 0x125 in mode 8 (FUN_005dead0),
// unit flags &= ~0x4020000, the scratch + 1, its think 0x33 frames on.
struct RiseIn {
    bool dead = false, hidden = false, unique = false, blocked = false, can_raise = false;
    int alignment = 0, scratch = 0, aip1 = 0;
};
enum class RiseKind { nothing, again, rise };
struct Rise {
    RiseKind kind = RiseKind::nothing;
    int frames = 0;                      // again: event 7, rise: its think, this many frames on
};
inline constexpr std::uint32_t kRiseFlagsKept = 0xfbfdffffu;   // unit flags (+0xc4) AND-ed after the skill starts
inline constexpr int kRiseSkill = 0x125, kRiseMode = 8, kRiseThink = 0x33, kRiseLimit = 2;
Rise ai_after_death_rise(const RiseIn& input);

// MonUMod 22's death hook (0x5a3250: event 1 into mode 0, by MonStats
// row). Five rows give MonUMod 21 (killself) to the monsters about them
// (FUN_005dfbf0(radius, first, end, undead only), the boss's seed): Andariel
// (FUN_005dfe00), Radament (FUN_005dfe20: end radamentdeath's Range - 100,
// at least 100), Izual (FUN_005e0020), Blood Raven (0x5dfd90, the undead
// only: its fourth argument is 1), overseer1 (FUN_005e0040). The Ancients'
// (FUN_005e0060) is a bare ret.
struct KillSelf { int monstats_row, radius, first, end; bool undead_only = false; };
// The five rows above, in MonStats row order; Radament's end from his Range.
std::array<KillSelf, 5> killself_hooks(int radamentdeath_range);

// Lightningdeath (MonUMod 42, a Shadow's sumumod) at its death,
// FUN_005a2910: its nova's level is its owner's Shadow Warrior level
// (skill 0x10c, a Shadow Master's too) / 2 + 1, 1..15; 1 when it died above
// 10 % life (FUN_00621f20).
int shadow_nova_level(int shadow_warrior_level, int life_percent);

std::pair<int, int> fire_blast(int max_life, int difficulty);
// What a Mana Burn monster's landed hit takes off a player's mana, in 256ths: the damage build (FUN_0057b7d0, a plain
// monster) rolls FUN_0057a8e0 over stats 62 / 63, which FUN_005a1f90 stores as `x << 8` and the roll shifts by 8 again,
// so `(least << 16)` up to (not including) `(most << 16)` on the unit's seed (`least` and `most` the whole points of
// Monster::mana_lo / mana_hi): 256 times the points the stat looks like.
int mana_burn_fine(int least, int most, Rng& rng);
// Spectral hit (FUN_005a3040 / FUN_005a21d0): each attack, one of five
// elements (0x6e21b8: fire, lightning, magic, cold, poison; as Skill::etype
// 0, 1, 4, 2, 3), MonLvl damage x MonUMod constants row 28 (min) and 31
// (max) %, the normal rows whatever the difficulty; cold and poison last 40
// frames longer.
inline constexpr std::array<int, 5> kSpectralElement{ 0, 1, 4, 2, 3 };

// Aura Enchanted (mod 30, FUN_005a1650): the aura and its level. The
// table at 0x73bf68 {min monster level, add, mul, div, skill}; the pick
// is rand(rows the monster's level reaches) on {name seed, 666}
// (FUN_00650e30 / FUN_0045c390); superunique 37 always takes Fanaticism.
// Level = (add + mlvl) x mul / div, 1..99.
struct BossAura { int skill = 0, level = 0; };
struct BossAuraRow { int min_lvl, add, mul, div, skill; };
inline constexpr BossAuraRow kRows[7] = { { 0, 0, 1, 6, 98 }, { 0, 0, 1, 6, 102 }, { 0, 0, 1, 5, 108 }, { 0, 0, 1, 7, 114 },
                                          { 0, 0, 1, 8, 123 }, { 0, 0, 1, 8, 122 }, { 20, 0, 1, 8, 118 } };
BossAura boss_aura(int mlvl, int name_seed, int super);

}  // namespace d2d::rules
