// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of aura_filter.hpp's functions; their notes are in the header.
#include "aura_filter.hpp"

#include <cstdint>

namespace d2d::rules {

bool aura_filter_takes(std::uint32_t filter, const AuraFilterMonster& monster) {
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

bool aura_walk_takes(std::uint32_t filter, const AuraFilterMonster& monster) {
    using namespace aura_filter;
    if (filter == 0) filter = kDefault;
    if ((filter & kSkipTownRooms) != 0 && monster.in_town) return false;
    return aura_filter_takes(filter, monster);
}

}
