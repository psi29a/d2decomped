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
#include <string>
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
                     fire = 9, champion = 16, lightning = 17, cold = 18, manahit = 25, teleport = 26, spectralhit = 27,
                     stoneskin = 28, multishot = 29, aura = 30, ghostly = 36, fanatic = 37, possessed = 38, berserk = 39;
}

namespace unique_detail {

// FUN_005a03e0: can monster `t` take mod `u`? Enabled; not an excluded
// MonType; fPick 2 (multishot) not for melee monsters.
// ponytail: fPick 1 / 3 (teleport, fast) test FUN_0046c140, not mapped —
// allowed here; MonType exclusions match the type itself, not its parents.
inline bool can_take(const UMod& mod, const MonType& type) {
    if (!mod.enabled) return false;
    if (!mod.exclude1.empty() && mod.exclude1 == type.montype) return false;
    if (!mod.exclude2.empty() && mod.exclude2 == type.montype) return false;
    if (mod.fpick == 2 && type.melee) return false;
    return true;
}

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
inline BossInfo roll_boss(const UMods& umods, const MonType& type, int difficulty, bool champions, Rng& seed, bool name = true) {
    using namespace unique_detail;
    const int difficulty_index = std::clamp(difficulty, 0, 2);
    BossInfo boss;
    if (champions && seed(100) < umods.constants[0]) {
        boss.kind = Boss::champion;
        if (const int id = pick(umods, type, seed, [&](const UMod& mod) { return mod.champion ? mod.cpick[std::size_t(difficulty_index)] : 0; })) boss.mods.push_back(id);
        boss.name_seed = int(seed.next() & 0xffff);
        return boss;
    }
    boss.kind = Boss::unique;
    const int count = std::min(seed(1) + 1 + difficulty_index, 8);
    for (int i = 0; i < count; ++i) {
        const int id = pick(umods, type, seed, [&](const UMod& mod) {
            return !mod.champion && std::ranges::find(boss.mods, mod.id) == boss.mods.end() ? mod.upick[std::size_t(difficulty_index)] : 0;
        });
        if (!id) break;
        boss.mods.push_back(id);
    }
    if (name) boss.name_seed = int(seed.next() & 0xffff);    // FUN_005a2120's fixed mods: 1 rndname first (after its minions)
    return boss;
}

// A superunique's mods (FUN_005a49b0): SuperUniques Mod1..3 (24 skipped),
// then `difficulty` more unique mods (upick, none twice), at most 5 before.
inline std::vector<int> superunique_mods(const UMods& umods, const MonType& type, const std::vector<int>& fixed, int difficulty, Rng& seed) {
    using namespace unique_detail;
    std::vector<int> mods;
    for (const int id : fixed) if (id != 24) mods.push_back(id);
    const int difficulty_index = std::clamp(difficulty, 0, 2);
    for (int i = 0; i < difficulty_index; ++i) {
        const int id = pick(umods, type, seed, [&](const UMod& mod) {
            return !mod.champion && std::ranges::find(mods, mod.id) == mods.end() ? mod.upick[std::size_t(difficulty_index)] : 0;
        });
        if (!id) break;
        mods.push_back(id);
    }
    return mods;
}

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

// A champion's name (client FUN_004ac870, run through the mod table at
// 0x724d78 for the fixed mods 1..4, then the monster's own; the last one
// wins): "%0 %1" (0x2b40) with the word for its champion mod from 0x6da488
// (16 "Champion" 0xc94, 36 Ghostly .. 39 Berserker 0x2b4c..0x2b4f) and its
// name. The search checks the first four keys; no match gives the fifth.
inline constexpr std::uint16_t kChampionFormat = 0x2b40;
inline constexpr std::array<std::pair<int, std::uint16_t>, 5> kWord{ { { 16, std::uint16_t(0xc94) }, { 36, std::uint16_t(0x2b4c) }, { 37, std::uint16_t(0x2b4d) },
                                                                          { 38, std::uint16_t(0x2b4e) }, { 39, std::uint16_t(0x2b4f) } } };
inline std::uint16_t champion_word(const std::vector<int>& mods) {
    std::uint16_t word = kWord[4].second;                          // fixed mod 1 (rndname) runs it first: no key matches
    for (const int id : mods) {
        if (id != 1 && id != 12 && id != 16 && (id < 36 || id > 39)) continue;
        std::size_t index = 0;
        while (index < 4 && kWord[index].first != id) ++index;
        word = kWord[index].second;
    }
    return word;
}

// What the mods do to a monster's stats (FUN_005a2120: the fixed rndname,
// hpmultiply, light, leveladd for uniques, then its own mods; champions
// and minions get theirs through `champion`). `boss` is the unique's /
// champion's own kind; a minion takes the minion rows.
struct BossStats {
    int level_add = 0;                          // leveladd +3; champion -1 on top (net +2)
    int exp_mult = 1;                           // unique x5, champion x3
    int hp_pct = 0;                             // + life %
    int dmg_pct = 0, tohit_pct = 0;             // + damage %, + attack rating %
    int velocity_pct = 0;                       // + speed % (fast; champion +20)
    std::array<int, 6> res_add{};               // + DR%, MR%, fire, light, cold, poison resist (ResDm..ResPo order)
    bool double_defense = false;                // stone skin
    int elem = -1, elem_min_pct = 0, elem_max_pct = 0;   // enchanted: the MonLvl damage % it adds as that element (0 fire, 1 light, 2 cold)
    int elem_len = 0;                           // ghostly: its cold's length (frames)
    int life_after_pct = 0;                     // possessed +100, berserker -75: after the hp% rows (FUN_005a0d20)
    int defense_pct = 0;                        // fanatic: item_armor_percent -70
    int phys_resist = -1;                       // ghostly: damageresist set to 80
};
// ponytail: resistance adds skip FUN_005a1370's "at most two immunities"
// bookkeeping only as far as the monster's own base resists count (they
// do).
// DifficultyLevels ChampionDamageBonus / UniqueDamageBonus (+0x34 / +0x30,
// 90 / 75 / 66): the share of the champion and strong damage and to-hit
// bonuses a difficulty keeps (FUN_005a0e80, FUN_005a17e0).
inline constexpr std::array<int, 3> kBossBonus{ 90, 75, 66 };
inline BossStats boss_stats(const UMods& umods, const MonType& type, Boss kind, const std::vector<int>& mods, int difficulty) {
    const int difficulty_index = std::clamp(difficulty, 0, 2);
    BossStats stats;
    const bool unique = kind == Boss::unique || kind == Boss::superunique;
    const bool champion = std::ranges::find(mods, umod::champion) != mods.end() || kind == Boss::champion;
    if (unique) {                                                        // FUN_005a0e40 leveladd, FUN_005a0dc0 hpmultiply
        stats.level_add += 3;
        stats.exp_mult = 5;
        stats.hp_pct = umods.constants[std::size_t(7 + difficulty_index)];
    } else if (kind == Boss::minion) {
        stats.hp_pct = umods.constants[std::size_t(1 + difficulty_index)];
    }
    // A champion's kind: 16 champion, or 36 ghostly .. 39 berserker (one
    // cpick each). All but the berserker run FUN_005a0e80 (level, exp,
    // damage and to-hit bonus, speed: ghostly -33, fanatic as `fast`,
    // else +20, none without a MonStats Velocity).
    const auto has = [&](int id) { return std::ranges::find(mods, id) != mods.end(); };
    const bool berserk = has(umod::berserk);
    if (champion) stats.hp_pct = umods.constants[std::size_t(4 + difficulty_index)];   // FUN_005a0dc0, the champion rows
    if (champion && !berserk) {                                          // FUN_005a0e80
        stats.level_add += unique ? -1 : 2;
        stats.exp_mult = 3;
        stats.tohit_pct += umods.constants[10] * kBossBonus[std::size_t(difficulty_index)] / 100;
        stats.dmg_pct += umods.constants[11] * kBossBonus[std::size_t(difficulty_index)] / 100;
        if (type.velocity > 0)
            stats.velocity_pct += has(umod::ghostly) ? -33 : has(umod::fanatic) ? std::clamp(2048 / type.velocity - 128, 10, 100) : 20;
    }
    if (has(umod::ghostly)) {                                            // FUN_005a1080: DR 80, cold from the champion rows, length 150
        stats.phys_resist = 80;
        stats.elem = 2;
        stats.elem_min_pct = umods.constants[std::size_t(22 + difficulty_index)];
        stats.elem_max_pct = umods.constants[std::size_t(25 + difficulty_index)];
        stats.elem_len = 150;
    }
    if (has(umod::fanatic)) stats.defense_pct = -70;                     // FUN_005a11f0
    if (has(umod::possessed)) stats.life_after_pct = 100;                // FUN_005a1230
    if (berserk) {                                                       // FUN_005a1280: life -75 %, +300 % x the bonus to damage and to-hit
        // ponytail: FUN_00463900 == 0x76 halves the damage part; that
        // class test isn't mapped.
        stats.life_after_pct = -75;
        stats.dmg_pct += 300 * kBossBonus[std::size_t(difficulty_index)] / 100;
        stats.tohit_pct += 300 * kBossBonus[std::size_t(difficulty_index)] / 100;
    }
    auto resist = [&](int id) {                                          // FUN_005a1370
        auto& resist_add = stats.res_add;
        auto base = [&](int index) { return type.diff[std::size_t(difficulty_index)].res[std::size_t(index)] + resist_add[std::size_t(index)]; };
        int immune = 0;
        for (int i = 0; i < 6; ++i) immune += base(i) > 99;
        if (immune >= 2) return;
        auto add = [&](int index, int value, int below) {
            if (base(index) < below) { resist_add[std::size_t(index)] += value; if (base(index) > 99) ++immune; }
        };
        switch (id) {
        case umod::resist:                                               // magic resistant: +40 cold, fire, light
            add(4, 40, 100); if (immune < 2) add(2, 40, 100); if (immune < 2) add(3, 40, 100); break;
        case umod::fire: resist_add[2] += 75; break;
        case umod::cold: resist_add[4] += 75; break;
        case umod::lightning: resist_add[3] += 75; break;
        case 23: resist_add[5] += 75; break;                                      // poison hit
        case 25: resist_add[1] += 20; break;                                      // mana burn: +20 magic resist
        case umod::spectralhit:                                          // +20 cold, fire, light under 75
            add(4, 20, 75); if (immune < 2) add(2, 20, 75); if (immune < 2) add(3, 20, 75); break;
        case umod::stoneskin: stats.double_defense = true; resist_add[0] += 50; break;
        default: break;
        }
    };
    for (const int id : mods) {
        switch (id) {
        case umod::strong:                                               // FUN_005a17e0
            stats.dmg_pct += umods.constants[kind == Boss::minion ? 14 : 15] * kBossBonus[std::size_t(difficulty_index)] / 100;
            stats.tohit_pct += umods.constants[kind == Boss::minion ? 12 : 13] * kBossBonus[std::size_t(difficulty_index)] / 100;
            break;
        case umod::fast:                                                 // FUN_005a1910: 2048 / Velocity - 128, 10..100
            if (type.velocity > 0) stats.velocity_pct += std::clamp(2048 / type.velocity - 128, 10, 100);
            break;
        case umod::fire: case umod::lightning: case umod::cold: {        // FUN_005a1990 & co: MonLvl damage x %
            stats.elem = id == umod::fire ? 0 : id == umod::lightning ? 1 : 2;
            const int base = kind == Boss::minion ? 16 : champion ? 22 : 28;
            stats.elem_min_pct = umods.constants[std::size_t(base + difficulty_index)];
            stats.elem_max_pct = umods.constants[std::size_t(base + 3 + difficulty_index)];
            resist(id);
            break;
        }
        default: resist(id); break;
        }
    }
    return stats;
}

// What a unique's mods do in the fight (the event hooks at 0x73c0b8, six
// a mod, run by FUN_005a4270; docs/research/re/monsters.md "Boss mods in
// the fight"). Only for the boss itself (flag 8), not its minions.
//
// Fire enchanted's death blast (FUN_005a2620): v = max life x
// MonsterCEDamagePercent (DifficultyLevels +0x3c: 50 / 35 / 20) / 100,
// then 3/4, 2/3 or 1/8 by difficulty; rolled from 60 % of v to v (the
// monster's seed), 64ths of it both physical and fire, within difficulty
// + 4 subtiles. Returns {lo, hi} before the roll.
inline std::pair<int, int> fire_blast(int max_life, int difficulty) {
    static constexpr int kCE[3] = { 50, 35, 20 };
    const int difficulty_index = std::clamp(difficulty, 0, 2);
    int value = max_life * kCE[difficulty_index] / 100;
    value = difficulty_index == 0 ? value - value / 4 : difficulty_index == 1 ? value - value / 3 : value / 8;
    return { value * 60 / 100, value };
}
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
inline BossAura boss_aura(int mlvl, int name_seed, int super) {
    mlvl = std::max(mlvl, 1);
    int count = 0;
    for (const auto& row : kRows) count += row.min_lvl <= mlvl;
    Rng rng{ std::uint32_t(name_seed) };
    int pick = rng(std::max(count, 1));
    if (super == 37) pick = 5;
    const auto& row = kRows[pick];
    return { row.skill, std::clamp((row.add + mlvl) * row.mul / row.div, 1, 99) };
}

}  // namespace d2d::rules
