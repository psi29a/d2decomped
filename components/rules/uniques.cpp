// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of uniques.hpp's functions, with the research behind each.
#include "uniques.hpp"

#include "montypes.hpp"
#include "rules.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::rules {
namespace unique_detail {

// An exclusion matches the type or a parent (FUN_005a0070, MonType equiv);
// the type alone is exact: the only one shipped, sandleaper, is no parent.
bool can_take(const UMod& mod, const MonType& type) {
    if (!mod.enabled) return false;
    if (!mod.exclude1.empty() && mod.exclude1 == type.montype) return false;
    if (!mod.exclude2.empty() && mod.exclude2 == type.montype) return false;
    if (mod.fpick == 2 && (type.melee || type.no_multishot)) return false;
    if ((mod.fpick == 1 && !type.can_swing) || (mod.fpick == 3 && !type.can_walk)) return false;
    return true;
}

}
}

namespace d2d::rules {

BossInfo roll_boss(const UMods& umods, const MonType& type, int difficulty, bool champions, Rng& seed, bool name) {
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
std::vector<int> superunique_mods(const UMods& umods, const MonType& type, const std::vector<int>& fixed, int difficulty, Rng& seed) {
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

std::optional<ActBoss> act_boss(int monstats_row, int base) {
    using namespace umod;
    switch (base) {
    case 0x9c: return monstats_row == 0x2c3 ? ActBoss{ { poisonhit, fast, multishot }, false } : ActBoss{ { questcomplete }, false };   // Andariel, uber
    case 0xd3: if (monstats_row == 0x2c4) return ActBoss{ { fast, cold }, false }; return std::nullopt;                                  // Duriel: uber only
    case 0xf2: return monstats_row == 0x2c0 ? ActBoss{ { questcomplete, aura, lightning, resist, fast }, false } : ActBoss{ { questcomplete }, false };   // Mephisto
    case 0xf3: return monstats_row == 0x2c1 ? ActBoss{ { questcomplete, resist, fast }, false } : ActBoss{ { questcomplete }, false };   // Diablo
    case 0x100: return monstats_row == 0x2c2 ? ActBoss{ { fast, cold }, false } : ActBoss{ { questcomplete }, false };                   // Izual
    case 0x10b: return ActBoss{ { bloodraven, questcomplete }, true };
    case 0x11c: return ActBoss{ { poisonhit, questcomplete }, false };                                                                     // the Maggot Queen
    case 0x16e: return ActBoss{ { questcomplete }, false };                                                                                // the compelling orb
    case 0x192: return ActBoss{ { questcomplete }, false };                                                                                // the Smith, no quest call
    case 0x20e: return ActBoss{ { questcomplete }, false };                                                                                // Nihlathak
    case 0x220: return monstats_row == 0x2c5 ? ActBoss{ { questcomplete, cold, resist, fast }, false } : ActBoss{ { questcomplete }, false };   // Baal, uber
    default: return std::nullopt;
    }
}

// The ancients' gear (FUN_005b1c50, from FUN_005b1cf0 for base 0x21c): four
// items each from the table at 0x6e2c90 (8 bytes a row per class, rows 0x18
// apart: the code, then the BodyLocs row it's worn on), by MonStats row
// 0x21c..0x21e (Talic, Madawc, Korlic).

// In Nightmare the code is its record's ubercode (+0x88), in Hell its
// ultracode (+0x8c); the ring's are 0 (Misc.txt has neither column), so no
// ring is made past Normal (FUN_00573b20: FUN_00633640 finds no item). Each
// is magic (quality 4) at the unit's level, made as a drop (FUN_00559ce0:
// two game seed steps).
std::vector<std::pair<std::string, int>> ancient_gear(const Tables& tables, int monstats_row, int difficulty) {
    struct Worn { std::string_view code; int loc; };
    static constexpr std::array<std::array<Worn, 4>, 3> kGear{ {
        { { { "bsd", 4 }, { "tow", 5 }, { "fld", 3 }, { "hbt", 9 } } },   // Talic: rarm, larm, tors, feet
        { { { "tax", 4 }, { "tax", 5 }, { "hgl", 10 }, { "hbt", 9 } } },  // Madawc: rarm, larm, glov, feet
        { { { "vou", 4 }, { "rin", 6 }, { "fld", 3 }, { "crn", 1 } } },   // Korlic: rarm, rrin, tors, head
    } };
    std::vector<std::pair<std::string, int>> gear;
    if (monstats_row < 0x21c || monstats_row > 0x21e) return gear;
    for (const auto& [code, loc] : kGear[std::size_t(monstats_row - 0x21c)]) {
        const auto base = tables.item_base.find(std::string(code));
        if (base == tables.item_base.end()) continue;
        const std::string& made = difficulty == 1 ? base->second.ubercode : difficulty == 2 ? base->second.ultracode : base->first;
        if (!made.empty() && tables.item_base.contains(made)) gear.emplace_back(made, loc);
    }
    return gear;
}

int bar_name_colour(Boss kind, std::string_view monstats_id) {
    static constexpr std::array<std::string_view, 13> kGoldRows{ "diablo", "diabloclone", "mephisto", "duriel", "baalcrab", "baalclone", "radament",
                                                                 "uberduriel", "uberandariel", "uberizual", "uberdiablo", "uberbaal", "ubermephisto" };
    if (std::ranges::contains(kGoldRows, monstats_id)) return kNameGold;
    return kind == Boss::champion ? kNameBlue : kind == Boss::unique || kind == Boss::superunique ? kNameGold : kNameWhite;
}

int bar_life_share(int life, int max_life) {
    if (max_life <= 0) return 0x80;
    return std::clamp(int((std::int64_t(life) << 7) / max_life), 2, 0x80);
}

std::vector<Immunity> immunity_lines(std::string_view monstats_id, const std::array<int, 6>& resists) {
    static constexpr std::array<std::string_view, 6> kUbers{ "uberduriel", "uberandariel", "uberizual", "uberdiablo", "uberbaal", "ubermephisto" };
    std::vector<Immunity> lines;
    if (std::ranges::contains(kUbers, monstats_id)) return lines;
    for (std::size_t index = 0; index < kImmunities.size(); ++index)
        if (resists[index] > 99) lines.push_back(kImmunities[index]);
    return lines;
}

// The MonSounds row a monster plays (client FUN_004ca410, UnitSnd.cpp): a
// superunique's own SuperUniques MonSound (record +0x18) when set (the
// Countess's, the Smith's); else, for flag 8 or 0x10 (champions, uniques,
// superuniques, minions), MonStats UMonSound (+0x16) when set (zombies:
// zombieunique); else MonStats MonSound (+0x14).
const std::string& boss_sound(const MonType& type, Boss kind, const std::string& superunique_sound) {
    if (kind == Boss::superunique && !superunique_sound.empty()) return superunique_sound;
    if (kind != Boss::none && !type.usound.empty()) return type.usound;
    return type.sound;
}

std::uint16_t champion_word(const std::vector<int>& mods) {
    std::uint16_t word = kWord[4].second;                          // fixed mod 1 (rndname) runs it first: no key matches
    for (const int id : mods) {
        if (id != 1 && id != 12 && id != 16 && (id < 36 || id > 39)) continue;
        std::size_t index = 0;
        while (index < 4 && kWord[index].first != id) ++index;
        word = kWord[index].second;
    }
    return word;
}

BossStats boss_stats(const UMods& umods, const MonType& type, Boss kind, const std::vector<int>& mods, int difficulty) {
    const int difficulty_index = std::clamp(difficulty, 0, 2);
    BossStats stats;
    const bool unique = kind == Boss::unique || kind == Boss::superunique;
    const bool champion = std::ranges::find(mods, umod::champion) != mods.end() || kind == Boss::champion;
    if (unique) {                                                        // FUN_005a0e40 leveladd, FUN_005a0dc0 hpmultiply
        stats.level_add += 3;
        stats.exp_mult = 5;
        stats.hp_pct = umods.constants[std::size_t(7 + difficulty_index)];
    } else if (kind == Boss::minion) {                                   // the same inits on its minions, flag 0 (FUN_0058f380 -> FUN_005a2100)
        stats.level_add += 3;                                            // FUN_005a0e40 tests no flag
        stats.exp_mult = 5;
        stats.hp_pct = umods.constants[std::size_t(1 + difficulty_index)];
    }
    // A champion's kind: 16 champion, or 36 ghostly .. 39 berserker (one
    // cpick each). All but the berserker run FUN_005a0e80 (level, exp,
    // damage and to-hit bonus, speed: none for ghostly, fanatic as `fast`,
    // else +20, none without a MonStats Velocity).
    const auto has = [&](int id) { return std::ranges::find(mods, id) != mods.end(); };
    const bool berserk = has(umod::berserk);
    // Champion, berserker and Extra Strong halve the damage bonus for the willowisp BaseId.
    const int damage_divisor = type.base == 0x76 ? 2 : 1;
    if (champion) stats.hp_pct = umods.constants[std::size_t(4 + difficulty_index)];   // FUN_005a0dc0, the champion rows
    if (champion && berserk && !unique) {                                // FUN_005a1280 runs no FUN_005a0e80: the fixed leveladd alone
        stats.level_add += 3;
        stats.exp_mult = 5;
    }
    if (champion && !berserk) {                                          // FUN_005a0e80
        stats.level_add += unique ? -1 : 2;
        stats.exp_mult = 3;
        stats.tohit_pct += umods.constants[10] * kBossBonus[std::size_t(difficulty_index)] / 100;
        stats.dmg_pct += (umods.constants[11] * kBossBonus[std::size_t(difficulty_index)] / 100) / damage_divisor;
        // Ghostly returns before the speed (0x5a0ff8): its -33 store at 0x5a1062 is never reached.
        if (type.velocity > 0 && !has(umod::ghostly))
            stats.velocity_pct += has(umod::fanatic) ? std::clamp(2048 / type.velocity - 128, 10, 100) : 20;
    }
    if (has(umod::ghostly)) {                                            // FUN_005a1080: DR 80, cold from the champion rows, length 150
        stats.phys_resist = 80;
        stats.elem_pct[2] = std::pair{ umods.constants[std::size_t(22 + difficulty_index)], umods.constants[std::size_t(25 + difficulty_index)] };
        stats.elem_len = 150;
    }
    if (has(umod::fanatic)) stats.defense_pct = -70;                     // FUN_005a11f0
    if (has(umod::possessed)) stats.life_after_pct = 100;                // FUN_005a1230
    if (berserk) {                                                       // FUN_005a1280: life -75 %, +300 % x the bonus to damage and to-hit
        const int bonus = 300 * kBossBonus[std::size_t(difficulty_index)] / 100;
        stats.life_after_pct = -75;
        stats.dmg_pct += bonus / damage_divisor;
        stats.tohit_pct += bonus;
    }
    auto resist = [&](int id) {                                          // FUN_005a1370
        if (kind == Boss::minion) return;                                // its flag 0: a minion takes no resist, no stone skin
        auto& resist_add = stats.res_add;
        auto base = [&](int index) { return type.diff[std::size_t(difficulty_index)].res[std::size_t(index)] + resist_add[std::size_t(index)]; };
        if (id == umod::stoneskin) stats.double_defense = true;
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
        case umod::poisonhit: resist_add[5] += 75; break;
        case 25: resist_add[1] += 20; break;                                      // mana burn: +20 magic resist
        case umod::spectralhit:                                          // +20 cold, fire, light under 75
            add(4, 20, 75); if (immune < 2) add(2, 20, 75); if (immune < 2) add(3, 20, 75); break;
        case umod::stoneskin: resist_add[0] += 50; break;
        default: break;
        }
    };
    for (const int id : mods) {
        switch (id) {
        case umod::strong:                                               // FUN_005a17e0
            stats.dmg_pct += (umods.constants[kind == Boss::minion ? 14 : 15] * kBossBonus[std::size_t(difficulty_index)] / 100) / damage_divisor;
            stats.tohit_pct += umods.constants[kind == Boss::minion ? 12 : 13] * kBossBonus[std::size_t(difficulty_index)] / 100;
            break;
        case umod::fast:                                                 // FUN_005a1910: 2048 / Velocity - 128, 10..100
            if (type.velocity > 0) stats.velocity_pct += std::clamp(2048 / type.velocity - 128, 10, 100);
            break;
        case umod::fire: case umod::lightning: case umod::cold: {        // FUN_005a1990 & co: MonLvl damage x %
            const int base = kind == Boss::minion ? 16 : champion ? 22 : 28;
            stats.elem_pct[id == umod::fire ? 0 : id == umod::lightning ? 1 : 2] = std::pair{ umods.constants[std::size_t(base + difficulty_index)],
                                                                                              umods.constants[std::size_t(base + 3 + difficulty_index)] };
            if (id == umod::cold) { stats.elem_len = 100; stats.elem_len_per_level = 5; }   // FUN_005a1c70: the length 100 + level x 5
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

// Fire enchanted's death blast (FUN_005a2620): v = max life x
// MonsterCEDamagePercent (DifficultyLevels +0x3c: 50 / 35 / 20) / 100,
// then 3/4, 2/3 or 1/8 by difficulty; rolled from 60 % of v to v (the
// monster's seed), 64ths of it both physical and fire, within difficulty
// + 4 subtiles. Returns {lo, hi} before the roll.
// FUN_005b21b0 (from the spawn, FUN_005b2a00): the mods a whole family
// carries, boss or not (FUN_005a4850 with 0: not counted as a boss), by
// MonStats BaseId. Built: scarab (20) and poisondead (10); the rest have
// no hooks d2d needs yet (rage, 13, has none in 0x73c0b8 at all).
std::vector<int> family_mods(int base_id) {
    switch (base_id) {
        case 0x18: return { umod::rage };                              // brute2: Yeti, Crusher, Brute, Wailing / Gargantuan Beast
        case 0x5b: return { umod::scarab };                            // scarab1: Dung Soldier .. Steel Weevil
        case 0x60: return { umod::poisondead };                        // mummy1: Dried Corpse .. Cadaver
        case 0xd3: return { umod::durieldead, umod::questcomplete };   // Duriel
        case 0x146: case 0x147: case 0x148: case 0x149: case 0x14a: case 0x162: return { umod::spcdamage };   // traps
        case 0x154: case 0x155: case 0x156: case 0x157: return { umod::partydead };                          // Diablo's bone prisons
        case 0x1b4: return { umod::ai_after_death };                   // reanimatedhorde1 (Act 5)
        case 0x1cd: return { umod::suicideminion };                    // minion8 (Act 5)
        case 0x1f5: return { umod::shatter };                          // frozenhorror1 (Act 5)
        case 0x21c: case 0x21d: case 0x21e: return { umod::questcomplete };
        default: return {};
    }
}

SpcDamage spcdamage(int base_id, std::optional<int> area_level) {
    SpcDamage out;
    out.level = area_level ? (*area_level < 2 ? 1 : *area_level) : 2;
    out.to_hit = std::min(out.level, 40) + 50;   // capped first: a custom row can hold INT_MAX
    const int half = out.level >> 1, level = out.level;
    switch (base_id) {
        case 0x146: out.list = { { 0x15, 0 }, { 0x16, 0 }, { 0x30, half }, { 0x31, level * 3 >> 1 } }; break;
        case 0x147: case 0x148: out.list = { { 0x15, std::min(half + 1, level) }, { 0x16, level } }; break;
        case 0x149: out.list = { { 0x15, 0 }, { 0x16, 0 }, { 0x39, level }, { 0x3a, level * 2 }, { 0x3b, level * 2 } }; break;
        case 0x14a: case 0x171: out.list = { { 0x15, 0 }, { 0x16, 0 }, { 0x32, half }, { 0x33, level * 3 >> 1 } }; break;
        default: break;
    }
    return out;
}

AfterDeathQueue ai_after_death_queue(int mode, int alignment, int chance, int aip1, Rng& rng) {
    AfterDeathQueue out;
    if (mode != 0 || alignment != 0) return out;
    out.cleared = true;
    if (rng(100) < chance) out.frames = aip1 * 10 + 1;
    return out;
}

Rise ai_after_death_rise(const RiseIn& input) {
    if (!input.dead || input.hidden || input.unique || input.alignment == 2) return {};
    if (input.blocked) return { RiseKind::again, input.aip1 * 10 + 1 };
    if (input.scratch >= kRiseLimit || !input.can_raise || input.alignment != 0) return {};
    return { RiseKind::rise, kRiseThink };
}

std::array<KillSelf, 5> killself_hooks(int radamentdeath_range) {
    return { { { 0x9c, 0x23, 1, 0x33 }, { 0xe5, 0x23, 0x28, std::max(radamentdeath_range - 100, 100) }, { 0x100, 0x69, 1, 2 }, { 0x10b, 0x23, 0x19, 0x7d, true },
               { 0x1df, 0x23, 0x19, 0x7d } } };
}

int shadow_nova_level(int shadow_warrior_level, int life_percent) {
    const int level = std::clamp(shadow_warrior_level / 2 + 1, 1, 15);
    return life_percent > 10 ? 1 : level;
}

std::pair<int, int> fire_blast(int max_life, int difficulty) {
    static constexpr int kCE[3] = { 50, 35, 20 };
    const int difficulty_index = std::clamp(difficulty, 0, 2);
    int value = max_life * kCE[difficulty_index] / 100;
    value = difficulty_index == 0 ? value - value / 4 : difficulty_index == 1 ? value - value / 3 : value / 8;
    return { value * 60 / 100, value };
}

// What a Mana Burn monster's landed hit takes off a player's mana, in 256ths: the damage build (FUN_0057b7d0, a plain
// monster) rolls FUN_0057a8e0 over stats 62 / 63, which FUN_005a1f90 stores as `x << 8` and the roll shifts by 8 again,
// so `(least << 16)` up to (not including) `(most << 16)` on the unit's seed (`least` and `most` the whole points of
// Monster::mana_lo / mana_hi): 256 times the points the stat looks like.

// FUN_0057c6c0 then takes it off the player's mana (FUN_0057aa60). tools/emu/mana_burn.py checks it
// against game.exe.
int mana_burn_fine(int least, int most, Rng& rng) {
    const int low = least << 16, high = most << 16;
    return high > low ? low + rng(high - low) : low;
}

BossAura boss_aura(int mlvl, int name_seed, int super) {
    mlvl = std::max(mlvl, 1);
    int count = 0;
    for (const auto& row : kRows) count += row.min_lvl <= mlvl;
    Rng rng{ std::uint32_t(name_seed) };
    int pick = rng(std::max(count, 1));
    if (super == 37) pick = 5;
    const auto& row = kRows[pick];
    return { row.skill, std::clamp((row.add + mlvl) * row.mul / row.div, 1, 99) };
}



}
