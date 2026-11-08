// SPDX-License-Identifier: GPL-3.0-or-later
// A skill's aurafilter (Skills.txt, skill +0x50) on a monster: FUN_0056b3e0,
// the test FUN_0056b7e0 runs on each unit within an aura's range. The
// friendly aura's do (FUN_005cf010) and the enemy one's (FUN_005cf3a0)
// both pass +0x50 in. The paladin's friendly auras have 0x12003 (Meditation
// 0x12001, the totems 0x10103); Holy Fire and Holy Shock 0xa783,
// Conviction 0xa583, Holy Freeze 0x4a283, Sanctuary 0xe786.
#pragma once

#include <cstdint>

namespace d2d::rules {

namespace aura_filter {
inline constexpr std::uint32_t kDefault = 0x583;          // FUN_0056b7e0: a filter of 0
inline constexpr std::uint32_t kMonsters = 0x2;           // unit type 1
inline constexpr std::uint32_t kUndead = 0x4;             // FUN_0063e990: MonStats +0xd & 8 or & 0x10 (lUndead, hUndead)
inline constexpr std::uint32_t kCorpses = 0x1000;         // set: dead ones only (mode 0xc); clear: living ones only
inline constexpr std::uint32_t kNoBosses = 0x4000;        // FUN_0063e9f0: not MonStats +0xc & 0x40 (boss)
inline constexpr std::uint32_t kEnemies = 0x8000;         // FUN_00554200: FUN_00650d70 says another side
inline constexpr std::uint32_t kAllies = 0x10000;         // FUN_00554de0: FUN_00650d70 says the same side
inline constexpr std::uint32_t kNoPrimeEvils = 0x40000;   // FUN_0063edc0: not MonStats +0xc & 0x80 (primeevil)
}  // namespace aura_filter

struct AuraFilterMonster {
    bool dead = false;
    bool undead = false;
    bool boss = false;
    bool prime_evil = false;
    bool ally = false;        // the caster's side: its owner's (FUN_0058f0d0), else FUN_00650d70
};

// Whether an aura with `filter` takes this monster. kEnemies and kAllies
// read the same FUN_00650d70 and take opposite answers, so a monster that
// is not the caster's ally never gets a friendly aura (0x12003).
// ponytail: the unit flags (0x80: +0xc4 & 4, 0x400: & 8), the town rooms
// (0x100, and 0x2000 in FUN_0056b7e0), line of sight (0x200,
// FUN_0064e260), 0x20000 (FUN_00622c40) and 0x80000 (state 0x56) aren't
// read.
[[nodiscard]] inline bool aura_filter_takes(std::uint32_t filter, const AuraFilterMonster& monster) {
    using namespace aura_filter;
    if (filter == 0) filter = kDefault;
    if ((filter & kMonsters) == 0) return false;
    if (monster.dead != ((filter & kCorpses) != 0)) return false;
    if ((filter & kUndead) != 0 && !monster.undead) return false;
    if ((filter & kNoBosses) != 0 && monster.boss) return false;
    if ((filter & kNoPrimeEvils) != 0 && monster.prime_evil) return false;
    if ((filter & kEnemies) != 0 && monster.ally) return false;
    if ((filter & kAllies) != 0 && !monster.ally) return false;
    return true;
}

}  // namespace d2d::rules
