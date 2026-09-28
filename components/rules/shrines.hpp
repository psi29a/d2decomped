// Shrines and chests (objects.txt InitFn 1 / OperateFn 2 and OperateFn 4),
// from game.exe 1.14d. docs/research/re/objects.md.
#pragma once

#include "rules.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace d2d::rules {

// A Shrines.txt row (its index is the shrine's id).
struct ShrineRow { int code = 0, arg0 = 0, arg1 = 0, duration = 0, reset = 0, effectclass = 0, level_min = 0; };

// FUN_0054f9d0: which shrine an objects.txt shrine is, on the game's
// object seed (`rgn`, as every InitFn: FUN_0054f5d0 hands them
// FUN_00546fa0's). Parm0 0: any row (1 + rand(count - 1)); 1 health
// (class 2), 2 mana (class 3); else a step, one in ten magic (1), the rest
// boosts (4). A class picks rand(n) of its rows (FUN_0054f770;
// FUN_00546c60 lists them in row order). Both re-roll, 8 tries at most,
// while `level_id` is under the row's LevelMin. The exchanges become
// boosts (5 -> 3, 4 -> 2), Enirhs a gem shrine.
inline int roll_shrine(const std::vector<ShrineRow>& rows, int parm0, int level_id, Rng& rgn) {
    Rng& obj = rgn;
    if (rows.size() < 2) return 0;
    auto low = [&](int id) { return level_id < rows[std::size_t(id)].level_min; };
    int id = 0;
    if (parm0 == 0) {
        for (int tries = 8; tries > 0; --tries) if (id = obj(int(rows.size()) - 1) + 1; !low(id)) break;
    } else {
        int cls = parm0 == 1 ? 2 : parm0 == 2 ? 3 : obj.next() % 10 == 0 ? 1 : 4;
        std::vector<int> list;
        for (std::size_t row = 0; row < rows.size(); ++row) if (rows[row].effectclass == cls) list.push_back(int(row));
        if (list.empty()) return 0;
        for (int tries = 8; tries > 0; --tries) if (id = std::max(list[std::size_t(rgn(int(list.size())))], 1); !low(id)) break;
    }
    return id == 5 ? 3 : id == 4 ? 2 : id == 16 ? 18 : id;
}

// A booster's stats (FUN_00583b30 through the table at 0x6e1850: Arg0 on
// its stat; combat FUN_005839b0: Arg0 % of the attack rating as tohit (19)
// and Arg1 damagepercent (25); stamina FUN_00583a70: its stamina filled,
// staminarecoverybonus (28) 1000), by Code. `ar`: the player's rating.
// Skills FUN_00583bf0: +Arg0 all skills (item_allskills, 127).
inline std::vector<std::pair<int, int>> shrine_boost(const ShrineRow& shrine, int attack_rating) {
    switch (shrine.code) {
    case 6:  return { { 171, shrine.arg0 } };                  // skill_armor_percent
    case 7:  return { { 19, attack_rating * shrine.arg0 / 100 }, { 25, shrine.arg1 } };
    case 8:  return { { 39, shrine.arg0 } };                   // fireresist
    case 9:  return { { 43, shrine.arg0 } };                   // coldresist
    case 10: return { { 41, shrine.arg0 } };                   // lightresist
    case 11: return { { 45, shrine.arg0 } };                   // poisonresist
    case 12: return { { 127, shrine.arg0 } };                  // item_allskills
    case 13: return { { 27, shrine.arg0 } };                   // manarecoverybonus
    case 14: return { { 28, 1000 } };                     // staminarecoverybonus
    case 15: return { { 85, shrine.arg0 } };                   // item_addexperience
    default: return {};
    }
}

// The recharges (FUN_005828e0 .. FUN_005829a0) on life / mana / their
// maxima, any fixed point: 1 both full, 2 life full, 3 mana full, 4 life
// down Arg0 %, mana up Arg1 % of that; 5 the other way round.
// (4 and 5 aren't rolled — roll_shrine turns them into 2 and 3.)
inline void shrine_recharge(const ShrineRow& shrine, std::int64_t& life, std::int64_t max_life, std::int64_t& mana, std::int64_t max_mana) {
    switch (shrine.code) {
    case 1: life = std::max(life, max_life); mana = std::max(mana, max_mana); break;
    case 2: life = std::max(life, max_life); break;
    case 3: mana = std::max(mana, max_mana); break;
    case 4: { const auto moved = life * shrine.arg0 / 100; life -= moved; mana += moved * shrine.arg1 / 100; break; }
    case 5: { const auto moved = mana * shrine.arg0 / 100; mana -= moved; life += moved * shrine.arg1 / 100; break; }
    default: break;
    }
}

// The gem shrine (FUN_00582c40): the first gem in the inventory with a
// better grade (misc.txt BetterGem) goes up one (FUN_00582ac0); with none,
// a chipped gem, rand(6) on the player's seed. Returns the code to give,
// "" when `upgrade` took one.
inline std::string gem_shrine(const Tables& tables, std::vector<d2s::Item>& items, Rng& seed) {
    for (auto& item : items) {
        if (item.location != 0 || item.panel != 1) continue;
        const auto found = tables.item_base.find(item.code);
        if (found == tables.item_base.end() || found->second.better_gem.empty() || found->second.better_gem == "non") continue;
        item.code = found->second.better_gem;
        return {};
    }
    static constexpr const char* kChipped[6] = { "gcw", "gcr", "gcg", "gcb", "gcy", "gcv" };
    return kChipped[seed(6)];
}

// FUN_00585b90: a chest's treasure class, "Act %d%s Chest %s" (FUN_0065a2c0:
// "", " (N)", " (H)"; A..C). The act's two marker levels (0x6e1988: act 1
// Blood Moor and Catacombs 4, 2 Lut Gholein .. , by area level) split a
// third each way: under lo + third A, under lo + 2 thirds B, else C.
inline std::string chest_tc(int act, int difficulty, int alvl, int lo_alvl, int hi_alvl) {
    const int third = (std::abs(hi_alvl - lo_alvl) + 1) / 3;
    const int cls = alvl < lo_alvl + third ? 0 : alvl < lo_alvl + 2 * third ? 1 : 2;
    static constexpr const char* kDifficultySuffix[3] = { "", " (N)", " (H)" };
    return std::format("Act {}{} Chest {}", std::clamp(act, 0, 4) + 1, kDifficultySuffix[std::clamp(difficulty, 0, 2)], char('A' + cls));
}
// A chest as its init makes it (InitFn 3, FUN_0054fcb0, on the game's
// object seed): first the trap (FUN_0054fbb0: rand(100) < MonLvl1 / 8 + 5, then
// a type 1..8, FUN_004bc500), then objects.txt Lockable chests lock at
// rand(100) < MonLvl1 / 2 + 8 (flag 0x80); one more step. MonLvl1: the
// classic normal column (Levels +0x10), whatever the difficulty.
struct ChestInit { int trap = 0; bool locked = false; };
inline ChestInit roll_chest(int mlvl1, bool lockable, Rng& seed) {
    ChestInit chest;
    if (seed(100) < mlvl1 / 8 + 5) chest.trap = seed.range(1, 8);
    if (lockable && seed(100) < mlvl1 / 2 + 8) chest.locked = true;
    seed.next();
    return chest;
}
// Opening it (FUN_00585f60): a locked one takes a key and drops two
// rounds; any other is empty one time in four (rand(100) < 25).
// ponytail: objects.txt 397's own drop table and the guaranteed drop
// behind FUN_005540d0 aren't here.
inline int chest_rounds(bool locked, Rng& seed) {
    const bool full = seed(100) > 24;
    return locked ? 2 : full ? 1 : 0;
}
// What a trap springs (the table at 0x732cec): 1..4 and 6 a trap monster
// at the chest (FUN_00582420) that acts once (its Trap-* AI, aip2 1) and
// dies: 1 trap-lightning's MissA1 chainlightning, 2 / 6 trap-firebolt's
// trapfirebolt, 3 trap-poisoncloud's Skill1 PrimePoisonNova (two rings of
// primepoisoncloud), 4 trap-nova's Skill1 Trap Nova (a nova of trapnova);
// 5 / 7 fires (FUN_00582380), 8 undead (FUN_005822f0).
// The missiles a unique's mods fire (uniques.hpp): loaded with the rest.
inline constexpr std::array<const char*, 3> kBossMissile{ "lightunique", "coldunique", "monstercorpseexplode" };
inline constexpr std::array<const char*, 9> kTrapMissile{ "", "chainlightning", "trapfirebolt", "primepoisoncloud", "trapnova",
                                                          "", "trapfirebolt", "", "" };
// The trap's missile level: DifficultyLevels MonsterSkillBonus (+0x10:
// 0 / 3 / 7) + 1 — a mode's missile (FUN_005a6d50) and a monster's skills
// (FUN_00573cb0, Sk1lvl 1) alike.
inline constexpr std::array<int, 3> kTrapLevel{ 1, 4, 8 };
// PrimePoisonNova (do 99, FUN_005ccd10): the offsets at 0x6e31e8 / 0x6e31a8;
// even ones at Param1 << 6, the odd ones between at Param2 << 6 (calc2 2).
inline constexpr std::array<std::pair<int, int>, 16> kPoisonNova{ { { 0, 2 }, { 1, 2 }, { 2, 2 }, { 2, 1 }, { 2, 0 }, { 2, -1 }, { 2, -2 }, { 1, -2 },
                                                                    { 0, -2 }, { -1, -2 }, { -2, -2 }, { -2, -1 }, { -2, 0 }, { -2, 1 }, { -2, 2 }, { -1, 2 } } };
// Trap 8's family (FUN_005474c0): the first of the level's region monsters
// (MonStats rows = game ids below the Expansion row) that's a zombie
// (mummy in act 2, `act` 0-based) or a skeleton, archer or skeleton mage:
// that family's first id. None: 234, a flying scimitar — and in act 1
// (FUN_00582250) no trap at all (-1).
inline int trap_undead(const std::vector<int>& region, int act) {
    const int chance = act == 1 ? 96 : 5;
    for (const int id : region) {
        if (id >= chance && id < chance + 5) return chance;
        for (const int first : { 0, 170, 274, 379, 383, 387 }) if (id >= first && id < first + 4) return first;
    }
    return act == 0 ? -1 : 234;
}

// The marker levels by act (0x6e1988).
inline constexpr std::array<std::pair<int, int>, 5> kChestLevels{ { { 2, 37 }, { 41, 73 }, { 76, 102 }, { 104, 108 }, { 109, 136 } } };

}  // namespace d2d::rules
