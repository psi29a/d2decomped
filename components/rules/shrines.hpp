// Shrines and chests (objects.txt InitFn 1 / OperateFn 2 and OperateFn 4),
// from game.exe 1.14d. docs/research/re/objects.md.
#pragma once

#include "rules.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace d2d::rules {

// A Shrines.txt row (its index is the shrine's id).
struct ShrineRow { int code = 0, arg0 = 0, arg1 = 0, duration = 0, reset = 0, effectclass = 0, level_min = 0; };

// FUN_0054f9d0: which shrine an objects.txt shrine is. Parm0 0: any row
// (1 + rand(count - 1), on `obj`, the object's seed); 1 health (class 2),
// 2 mana (class 3); else a step of `obj`, one in ten magic (1), the rest
// boosts (4). A class picks rand(n) of its rows (FUN_0054f770, on `rgn`,
// the game's object seed; FUN_00546c60 lists them in row order). Both
// re-roll, 8 tries at most, while `level_id` is under the row's LevelMin.
// The exchanges become boosts (5 -> 3, 4 -> 2), Enirhs a gem shrine.
inline int roll_shrine(const std::vector<ShrineRow>& rows, int parm0, int level_id, Rng& obj, Rng& rgn) {
    if (rows.size() < 2) return 0;
    auto low = [&](int id) { return level_id < rows[std::size_t(id)].level_min; };
    int id = 0;
    if (parm0 == 0) {
        for (int n = 8; n > 0; --n) if (id = obj(int(rows.size()) - 1) + 1; !low(id)) break;
    } else {
        int cls = parm0 == 1 ? 2 : parm0 == 2 ? 3 : obj.next() % 10 == 0 ? 1 : 4;
        std::vector<int> list;
        for (std::size_t r = 0; r < rows.size(); ++r) if (rows[r].effectclass == cls) list.push_back(int(r));
        if (list.empty()) return 0;
        for (int n = 8; n > 0; --n) if (id = std::max(list[std::size_t(rgn(int(list.size())))], 1); !low(id)) break;
    }
    return id == 5 ? 3 : id == 4 ? 2 : id == 16 ? 18 : id;
}

// A booster's stats (FUN_00583b30 through the table at 0x6e1850: Arg0 on
// its stat; combat FUN_005839b0: Arg0 % of the attack rating as tohit (19)
// and Arg1 damagepercent (25); stamina FUN_00583a70: its stamina filled,
// staminarecoverybonus (28) 1000), by Code. `ar`: the player's rating.
// ponytail: skill boost (12, FUN_00583bf0 +Arg0 all skills) isn't here —
// no all-skills stat in the fighter yet.
inline std::vector<std::pair<int, int>> shrine_boost(const ShrineRow& s, int ar) {
    switch (s.code) {
    case 6:  return { { 171, s.arg0 } };                  // skill_armor_percent
    case 7:  return { { 19, ar * s.arg0 / 100 }, { 25, s.arg1 } };
    case 8:  return { { 39, s.arg0 } };                   // fireresist
    case 9:  return { { 43, s.arg0 } };                   // coldresist
    case 10: return { { 41, s.arg0 } };                   // lightresist
    case 11: return { { 45, s.arg0 } };                   // poisonresist
    case 13: return { { 27, s.arg0 } };                   // manarecoverybonus
    case 14: return { { 28, 1000 } };                     // staminarecoverybonus
    case 15: return { { 85, s.arg0 } };                   // item_addexperience
    default: return {};
    }
}

// The recharges (FUN_005828e0 .. FUN_005829a0) on life / mana / their
// maxima, any fixed point: 1 both full, 2 life full, 3 mana full, 4 life
// down Arg0 %, mana up Arg1 % of that; 5 the other way round.
// (4 and 5 aren't rolled — roll_shrine turns them into 2 and 3.)
inline void shrine_recharge(const ShrineRow& s, std::int64_t& life, std::int64_t max_life, std::int64_t& mana, std::int64_t max_mana) {
    switch (s.code) {
    case 1: life = std::max(life, max_life); mana = std::max(mana, max_mana); break;
    case 2: life = std::max(life, max_life); break;
    case 3: mana = std::max(mana, max_mana); break;
    case 4: { const auto d = life * s.arg0 / 100; life -= d; mana += d * s.arg1 / 100; break; }
    case 5: { const auto d = mana * s.arg0 / 100; mana -= d; life += d * s.arg1 / 100; break; }
    default: break;
    }
}

// FUN_00585b90: a chest's treasure class, "Act %d%s Chest %s" (FUN_0065a2c0:
// "", " (N)", " (H)"; A..C). The act's two marker levels (0x6e1988: act 1
// Blood Moor and Catacombs 4, 2 Lut Gholein .. , by area level) split a
// third each way: under lo + third A, under lo + 2 thirds B, else C.
inline std::string chest_tc(int act, int difficulty, int alvl, int lo_alvl, int hi_alvl) {
    const int third = (std::abs(hi_alvl - lo_alvl) + 1) / 3;
    const int cls = alvl < lo_alvl + third ? 0 : alvl < lo_alvl + 2 * third ? 1 : 2;
    static constexpr const char* kD[3] = { "", " (N)", " (H)" };
    return std::format("Act {}{} Chest {}", std::clamp(act, 0, 4) + 1, kD[std::clamp(difficulty, 0, 2)], char('A' + cls));
}
// The marker levels by act (0x6e1988).
inline constexpr std::array<std::pair<int, int>, 5> kChestLevels{ { { 2, 37 }, { 41, 73 }, { 76, 102 }, { 104, 108 }, { 109, 136 } } };

}  // namespace d2d::rules
