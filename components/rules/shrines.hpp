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
#include <string_view>
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
// The trap alone is InitFn 2 (beds, bookshelves, rat nests, guard corpses).
inline int roll_trap(int mlvl1, Rng& seed) { return seed(100) < mlvl1 / 8 + 5 ? seed.range(1, 8) : 0; }
struct ChestInit { int trap = 0; bool locked = false; };
inline ChestInit roll_chest(int mlvl1, bool lockable, Rng& seed) {
    ChestInit chest;
    chest.trap = roll_trap(mlvl1, seed);
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
// A door operated (OperateFn 8, FUN_00581d40; its modes NU OP ON S1..S4 are
// 0..6), at least 500 ms after its last change (unit +0xd4): closed (0)
// opens (2); open, or stuck (5), it closes (0) when no one stands in it
// (FUN_0064d800, mask 0x8180), else it sticks at 5. -1: no change.
// ponytail: the 0x8000 blocker (mode 4) and the locked door (6, a key)
// aren't here: no Act 1 door starts in either.
inline int door_mode(int mode, bool occupied) {
    if (mode == 0) return 2;
    if (mode != 2 && mode != 5) return -1;
    return !occupied ? 0 : mode == 5 ? -1 : 5;
}
// The client's object sounds (0x7295f8 by objects.txt Id: a Sounds.txt
// row for each mode it enters, FUN_004cb380), for the objects operated
// here.
struct ObjectSound { int id; int mode; std::string_view sound; };
inline constexpr ObjectSound kObjectSounds[] = {
    { 13, 0, "object_door_metal_close" }, { 13, 2, "object_door_metal_open" }, { 14, 0, "object_door_metal_close" }, { 14, 2, "object_door_metal_open" },
    { 15, 0, "object_door_wood_close" }, { 15, 2, "object_door_wood_open" }, { 16, 0, "object_door_wood_close" }, { 16, 2, "object_door_wood_open" },
    { 23, 0, "object_door_gate_close" }, { 23, 2, "object_door_gate_open" }, { 24, 0, "object_door_gate_close" }, { 24, 2, "object_door_gate_open" },
    { 25, 0, "object_door_gate_close" }, { 25, 2, "object_door_gate_open" }, { 47, 0, "object_door_gate_close" }, { 47, 2, "object_door_gate_open" },
    { 27, 0, "object_door_wood_close" }, { 27, 1, "object_door_wood_open" },
    { 62, 0, "object_door_wood_close" }, { 62, 2, "object_door_wood_open" }, { 63, 0, "object_door_wood_close" }, { 63, 2, "object_door_wood_open" },
    { 64, 0, "object_door_wood_close" }, { 64, 2, "object_door_wood_open" }, { 74, 0, "object_door_wood_close" }, { 74, 2, "object_door_wood_open" },
    { 75, 0, "object_door_wood_close" }, { 75, 2, "object_door_wood_open" }, { 129, 1, "object_door_secret" },
    { 290, 0, "object_door_metal_close" }, { 290, 2, "object_door_metal_open" }, { 291, 0, "object_door_metal_close" }, { 291, 2, "object_door_metal_open" },
    { 292, 0, "object_door_metal_close" }, { 292, 2, "object_door_metal_open" }, { 293, 0, "object_door_metal_close" }, { 293, 2, "object_door_metal_open" },
    { 294, 0, "object_door_wood_close" }, { 294, 2, "object_door_wood_open" }, { 295, 0, "object_door_wood_close" }, { 295, 2, "object_door_wood_open" },
    { 1, 1, "object_casket" }, { 3, 1, "object_casket" }, { 50, 1, "object_casket" }, { 51, 1, "object_casket" }, { 53, 1, "object_casket" }, { 79, 1, "object_casket" },
    { 7, 1, "object_wood_break_1" }, { 46, 1, "object_wood_break_1" }, { 11, 1, "object_barrel_explode" }, { 28, 1, "object_grave" },
    { 54, 1, "object_corpse_roll" }, { 55, 1, "object_corpse_roll" }, { 56, 1, "object_corpse_roll" }, { 326, 1, "object_corpse_roll" },
    { 57, 1, "object_corpse_drop" }, { 58, 1, "object_corpse_drop" }, { 155, 1, "object_stone_large" }, { 159, 1, "object_stone_large" },
    { 163, 1, "object_shrine_hell_2" }, { 169, 1, "skeleton_walk_1" }, { 174, 1, "object_stone_small" }, { 175, 1, "object_stone_small" },
    { 247, 1, "object_bed" }, { 248, 1, "object_bed" }, { 289, 1, "object_bed" },
    { 104, 2, "object_armorstand" }, { 105, 2, "object_armorstand" }, { 106, 2, "object_weaponrack" }, { 107, 2, "object_weaponrack" },
    { 179, 2, "object_bookshelf" }, { 180, 2, "object_bookshelf" },
    { 111, 1, "object_well" }, { 111, 2, "object_well" }, { 113, 1, "object_well" }, { 113, 2, "object_well" }, { 115, 1, "object_well" }, { 115, 2, "object_well" },
    { 130, 1, "object_well" }, { 130, 2, "object_well" }, { 138, 1, "object_well" }, { 138, 2, "object_well" },
};
inline std::string_view object_sound(int id, int mode) {
    for (const auto& entry : kObjectSounds) if (entry.id == id && entry.mode == mode) return entry.sound;
    return {};
}
// An armor stand's armor (FUN_00584160 -> FUN_005594c0) or a weapon rack's
// weapon (FUN_005841d0 -> FUN_00559630): one of the spawnable bases up to
// item level `ilvl` (the area level, less one above 1), all alike
// (FUN_00555e70 / FUN_00555fb0: rand(count)). "" when there's none.
// ponytail: the pool is the auto armoN / weapN classes' (spawnable) bases in
// their order, not armor.txt / weapons.txt row order; the act re-roll in
// FUN_00555e00 and the rack's 6 tries past ItemTypes flag 2 aren't here.
inline std::string stand_item(const Tables& tables, bool weapon, int ilvl, Rng& seed) {
    std::vector<std::string> pool;
    for (int level = 3; level <= 87; level += 3)
        if (const auto found = tables.treasure.find(std::format("{}{}", weapon ? "weap" : "armo", level)); found != tables.treasure.end())
            for (const auto& [code, chance] : found->second.items)
                if (const auto base = tables.item_base.find(code); base != tables.item_base.end() && base->second.level <= std::max(ilvl, 1)) pool.push_back(code);
    return pool.empty() ? std::string() : pool[std::size_t(seed(int(pool.size())))];
}
// A well's drink (FUN_00585720): life (Parm3 & 2) and mana (& 1) up
// Parm1 / 256 of their maxima, stamina always; false when none was short
// (the well keeps its charge). Every well's Parm1 128, Parm3 3.
// ponytail: the poison / freeze cures and the merc's drink aren't here.
inline bool well_drink(std::int64_t& life, std::int64_t max_life, std::int64_t& mana, std::int64_t max_mana, std::int64_t& stamina, std::int64_t max_stamina) {
    bool drank = false;
    for (auto [value, max] : { std::pair{ &life, max_life }, { &mana, max_mana }, { &stamina, max_stamina } })
        if (*value < max) { *value = std::min(*value + (128 * max >> 8), max); drank = true; }
    return drank;
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
