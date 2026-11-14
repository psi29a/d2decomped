// SPDX-License-Identifier: GPL-3.0-or-later
// A skill's aurafilter (Skills.txt, skill +0x50) on a monster: FUN_0056b3e0,
// the test FUN_0056b7e0 runs on each unit within an aura's range. The
// friendly aura's do (FUN_005cf010) and the enemy one's (FUN_005cf3a0)
// both pass +0x50 in. The paladin's friendly auras have 0x12003 (Meditation
// 0x12001, the totems 0x10103); Holy Fire and Holy Shock 0xa783,
// Conviction 0xa583, Holy Freeze 0x4a283, Sanctuary 0xe786.
// tools/emu/aura_filter.py runs FUN_0056b3e0 on the same answers.
#pragma once

#include <cstdint>

namespace d2d::rules {

namespace aura_filter {
inline constexpr std::uint32_t kDefault = 0x583;          // FUN_0056b7e0: a filter of 0
inline constexpr std::uint32_t kMonsters = 0x2;           // unit type 1
inline constexpr std::uint32_t kUndead = 0x4;             // FUN_0063e990: MonStats +0xd & 8 or & 0x10 (lUndead, hUndead)
inline constexpr std::uint32_t kUnitFlag4 = 0x80;         // only a unit with flags +0xc4 & 4
inline constexpr std::uint32_t kNotInTown = 0x100;        // not in a town room (FUN_0061ab00 of FUN_00620bb0)
inline constexpr std::uint32_t kClearLine = 0x200;        // FUN_0064e260 from the caster's room and spot to the unit's, mask 4, finds nothing
inline constexpr std::uint32_t kUnitFlag8 = 0x400;        // only a unit with flags +0xc4 & 8
inline constexpr std::uint32_t kCorpses = 0x1000;         // set: dead ones only (mode 0xc); clear: neither dead nor dying (mode 0)
inline constexpr std::uint32_t kSkipTownRooms = 0x2000;   // FUN_0056b7e0 walks no town room (FUN_0061ab00)
inline constexpr std::uint32_t kNoBosses = 0x4000;        // FUN_0063e9f0: not MonStats +0xc & 0x40 (boss)
inline constexpr std::uint32_t kEnemies = 0x8000;         // FUN_00554200: FUN_00650d70 says another side
inline constexpr std::uint32_t kAllies = 0x10000;         // FUN_00554de0: FUN_00650d70 says the same side; and not MonStats +0xd & 1 (npc)
inline constexpr std::uint32_t kInMelee = 0x20000;        // FUN_00622c40(caster, unit, 0)
inline constexpr std::uint32_t kNoPrimeEvils = 0x40000;   // FUN_0063edc0: not MonStats +0xc & 0x80 (primeevil)
inline constexpr std::uint32_t kNotJustHit = 0x80000;     // FUN_00639df0: not in state 0x56 (justhit)
}  // namespace aura_filter

struct AuraFilterMonster {
    bool dead = false;          // mode 0xc (DD)
    bool dying = false;         // mode 0 (DT): no filter takes it
    bool undead = false;
    bool boss = false;
    bool prime_evil = false;
    bool ally = false;          // the caster's side: its owner's (FUN_0058f0d0), else FUN_00650d70
    bool npc = false;           // MonStats +0xd & 1 (npc)
    bool unit_flag_4 = true;    // unit flags +0xc4 & 4
    bool unit_flag_8 = true;    // unit flags +0xc4 & 8
    bool in_town = false;       // its room's level is a town (FUN_0061ab00)
    bool in_melee = false;      // FUN_00622c40(caster, it, 0)
    bool just_hit = false;      // state 0x56
    bool clear_line = true;     // FUN_0064e260 from the caster to it, mask 4, blocked nowhere
};

// Whether FUN_0056b3e0 with `filter` takes this monster, cast by a unit
// that isn't a missile and stands in a room. kEnemies and kAllies read the
// same FUN_00650d70 and take opposite answers, so a monster that is not
// the caster's ally never gets a friendly aura (0x12003), nor does an npc.
// No bit here has side effects, so the order is game.exe's for reading
// only. A filter of 0 takes nothing here; the walk makes it kDefault
// (aura_walk_takes).
[[nodiscard]] inline bool aura_filter_takes(std::uint32_t filter, const AuraFilterMonster& monster) {
    using namespace aura_filter;
    if ((filter & kMonsters) == 0) return false;
    if ((filter & kCorpses) != 0 ? !monster.dead : monster.dead || monster.dying) return false;
    if ((filter & kUndead) != 0 && !monster.undead) return false;
    if ((filter & kNoBosses) != 0 && monster.boss) return false;
    if ((filter & kNoPrimeEvils) != 0 && monster.prime_evil) return false;
    if ((filter & kUnitFlag4) != 0 && !monster.unit_flag_4) return false;
    if ((filter & kUnitFlag8) != 0 && !monster.unit_flag_8) return false;
    if ((filter & kNotInTown) != 0 && monster.in_town) return false;
    if ((filter & kAllies) != 0 && (!monster.ally || monster.npc)) return false;
    if ((filter & kEnemies) != 0 && monster.ally) return false;
    if ((filter & kInMelee) != 0 && !monster.in_melee) return false;
    if ((filter & kNotJustHit) != 0 && monster.just_hit) return false;
    if ((filter & kClearLine) != 0 && !monster.clear_line) return false;
    return true;
}

// The walk's own part (FUN_0056b7e0): a filter of 0 is kDefault, and
// kSkipTownRooms passes over a town room's units before FUN_0056b3e0.
[[nodiscard]] inline bool aura_walk_takes(std::uint32_t filter, const AuraFilterMonster& monster) {
    using namespace aura_filter;
    if (filter == 0) filter = kDefault;
    if ((filter & kSkipTownRooms) != 0 && monster.in_town) return false;
    return aura_filter_takes(filter, monster);
}

}  // namespace d2d::rules
