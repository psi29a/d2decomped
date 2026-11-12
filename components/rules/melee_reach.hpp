// SPDX-License-Identifier: GPL-3.0-or-later
// A unit's melee reach and a target on the move — game.exe 1.14d
// FUN_00622c40 and FUN_00622dc0 (tools/emu/melee_reach.py).
#pragma once

#include <unit_size.hpp>

namespace d2d::rules {

// Is a unit moving (FUN_00622dc0 → FUN_00622d00)? A player in mode 2, 3
// or 6 (walk, run, town walk); a monster in mode 2 or 0xf (walk, run), or
// a vulture (MonStats BaseId 0x6e, vulture1: FUN_00463860 → FUN_00463900)
// in 8 / 9 (S1 / S2). Any other type isn't (a missile asserts).
inline bool unit_moving(UnitType type, int mode, int base_id) {
    if (type == UnitType::kPlayer) return mode == 2 || mode == 3 || mode == 6;
    if (type != UnitType::kMonster) return false;
    if (mode == 2 || mode == 0xf) return true;
    return (mode == 8 || mode == 9) && base_id == 0x6e;
}

// Is the target in the unit's melee reach (FUN_00622c40(unit, target,
// extra))? `apart` is FUN_00641530's distance, `range` the unit's
// (FUN_00622870: a player's weapon range, a monster's MeleeRng). A target
// monster of BaseId 0x102 / 0x105 (a tentacle or its head) is in reach under range
// + 8; else at 0 or less always, and within range + `extra` + 1 when the
// line to it is clear (FUN_00622aa0 mask 0x804: `line_cut`, asked only then).
template <class LineCut>
bool melee_reach(int apart, int range, bool tentacle, int extra, LineCut&& line_cut) {
    if (tentacle && range + 8 > apart) return true;
    if (apart <= 0) return true;
    return range + extra + 1 >= apart && !line_cut();
}

}  // namespace d2d::rules
