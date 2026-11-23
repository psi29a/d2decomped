// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of melee_reach.hpp's functions; their notes are in the header.
#include "melee_reach.hpp"

#include "unit_size.hpp"

namespace d2d::rules {

bool unit_moving(UnitType type, int mode, int base_id) {
    if (type == UnitType::kPlayer) return mode == 2 || mode == 3 || mode == 6;
    if (type != UnitType::kMonster) return false;
    if (mode == 2 || mode == 0xf) return true;
    return (mode == 8 || mode == 9) && base_id == 0x6e;
}

}
