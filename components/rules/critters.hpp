// SPDX-License-Identifier: GPL-3.0-or-later
// Town critters (docs/research/re/critters.md): game.exe 1.14d FUN_0046c600.
#pragma once

#include "monster_ids.hpp"

namespace d2d::rules {

inline constexpr int kCritterPlayer = 0, kCritterMonster = 1;   // unit +0x00

// FUN_0046c600, FUN_0046c570's callback for a chicken or bunny: does a unit in
// its rooms count for its nearest one? A player, or a monster other than a
// chicken (0x95) or bunny (0x22c). Its dead test FUN_00464820 (0x46c62d) gets
// ECX = the critter, not the candidate, so a corpse counts (bugs.md row 110).
[[nodiscard]] constexpr bool critter_neighbour(bool critter_dead, int unit_type, int row) {
    if (critter_dead) return false;
    if (unit_type == kCritterPlayer) return true;
    return unit_type == kCritterMonster && row != monster_ids::kChicken && row != monster_ids::kBunny;
}

}  // namespace d2d::rules
