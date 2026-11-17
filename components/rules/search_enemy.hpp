// SPDX-License-Identifier: GPL-3.0-or-later
// A shift-click's own pick of a monster beside the player (Skills.txt
// SearchEnemyXY, FUN_00467660, game.exe 1.14d): cursor-units.md "The skill's
// own target search". The client runs it before it sends the skill, when
// the click has no unit under the cursor.
#pragma once

#include <array>
#include <cstddef>
#include <cstdlib>
#include <span>

namespace d2d::rules {

// A unit FUN_00641cb0 walks: its subtile, its size (FUN_00620510: a player
// 2, a monster MonStats2 SizeX), type and mode, and what FUN_00467660 asks
// of the one it finds: unit +0xc4 bit 2, MonStats2 shiftSel
// (FUN_004638a0(class, 6)) and whether the player may interact with it
// (FUN_00465c60).
struct SearchUnit {
    int x = 0, y = 0, size = 2, type = 1, mode = 1;
    bool flag4 = true, shift_sel = false, interact = true;
};

// FUN_00641cb0(room, x, y, filter, 0, 1): whether the unit stands on the
// spot. Players (not in mode 0 / 0x11), monsters (not 0 / 0xc) and missiles
// only; the size goes to 1..3 (under 1: never). 1 its own subtile, 2 the plus
// round it, 3 the 3 x 3 square.
[[nodiscard]] constexpr bool search_covers(const SearchUnit& unit, int spot_x, int spot_y) noexcept {
    const bool walked = (unit.type == 0 && unit.mode != 0 && unit.mode != 0x11) || (unit.type == 1 && unit.mode != 0 && unit.mode != 0xc) || unit.type == 3;
    if (!walked) return false;
    const int size = unit.size >= 3 ? 3 : unit.size;
    if (size < 1) return false;
    const int across = std::abs(spot_x - unit.x), down = std::abs(spot_y - unit.y);
    if (size == 1) return across == 0 && down == 0;
    if (size == 2) return across + down <= 1;
    return across <= 1 && down <= 1;
}

// The spots FUN_00467660 tries round the player, in order: the facing's
// eighth ((dir & 0xff) >> 3, dir its path's +0x65 in 64) plus 0, -1, 1, -2,
// 2 (0x6d6b34), at that eighth's step (x 0x6d6b14, y 0x6d6af4).
inline constexpr std::array<int, 5> kSearchTurns{ 0, -1, 1, -2, 2 };
inline constexpr std::array<int, 8> kSearchStepX{ 0, -2, -3, -2, 0, 2, 3, 2 };
inline constexpr std::array<int, 8> kSearchStepY{ 3, 2, 0, -2, -3, -2, 0, 2 };

// FUN_00467660: the first spot whose collision has a unit's own bit (0x180,
// `unit_bits(x, y)`) and the first unit there (units in walk order) a
// monster (filter LAB_00467640) with +0xc4 bit 2. With TargetableOnly
// (Skills.txt +6 bit 0x10) it must be shiftSel or one the player may
// interact with. A spot that fails moves on to the next spot, not the next
// unit. The pick's index in `units`, or -1.
template <class UnitBits>
[[nodiscard]] int search_enemy_xy(int dir, int player_x, int player_y, bool targetable_only, std::span<const SearchUnit> units, UnitBits&& unit_bits) {
    const int eighth = (dir & 0xff) >> 3;
    for (const int turn : kSearchTurns) {
        const auto way = std::size_t((turn + eighth) & 7);
        const int spot_x = player_x + kSearchStepX[way], spot_y = player_y + kSearchStepY[way];
        if (!unit_bits(spot_x, spot_y)) continue;
        std::size_t found = units.size();
        for (std::size_t index = 0; index < units.size(); ++index)
            if (units[index].type == 1 && search_covers(units[index], spot_x, spot_y)) {
                found = index;
                break;
            }
        if (found == units.size()) continue;
        const auto& unit = units[found];
        if (!unit.flag4) continue;
        if (targetable_only && !unit.shift_sel && !unit.interact) continue;
        return int(found);
    }
    return -1;
}

}  // namespace d2d::rules
