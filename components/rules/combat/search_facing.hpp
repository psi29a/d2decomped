// SPDX-License-Identifier: GPL-3.0-or-later
// The facing used by a client's SearchEnemyXY pick (FUN_006238c0).
#pragma once

#include "monsters.hpp"

namespace d2d::rules {

// FUN_006238c0: the player's facing in 64 as FUN_00467660 reads it. On its
// path's target (+0x10 / +0x12, SP1, both subtiles) it is the path's own facing
// (+0x65); anywhere else the way from its subtile to that target
// (FUN_00621dc0 -> FUN_0064fdc0, rules::direction64), walking or standing alike.
// tools/emu/search_facing.py.
[[nodiscard]] inline int search_facing(int unit_x, int unit_y, int target_x, int target_y, int new_dir) {
    if (unit_x == target_x && unit_y == target_y) return new_dir;
    return direction64(unit_x, unit_y, target_x, target_y);
}

}  // namespace d2d::rules
