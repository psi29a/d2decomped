// SPDX-License-Identifier: GPL-3.0-or-later
// A shift-click's own pick of a monster beside the player (Skills.txt
// SearchEnemyXY, FUN_00467660, game.exe 1.14d): cursor-units.md "The skill's
// own target search". The client runs it before it sends the skill, when
// the click has no unit under the cursor. And a click's pick of the unit
// nearest it (SearchEnemyNear, FUN_00467490), on every click of such a skill.
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

// A unit FUN_00467490 (SearchEnemyNear) may take: its unit type and mode, its
// subtile (a player's or monster's path, an item's static path +0xc / +0x10),
// unit +0xc4 bit 0x10000, whether the cursor's hit test FUN_00466870(unit, 0,
// 0, 0) passes, and, for an item, Items bitfield1 bit 1 (FUN_00629cc0, which
// Iron Golem's search asks for).
struct NearUnit {
    int type = 1, x = 0, y = 0, mode = 1;
    bool dead_flag = false, hit = true, metal = false;
};

// FUN_00464820: a player in mode 0 / 0x11, a monster in 0 / 0xc, or any unit
// with +0xc4 bit 0x10000.
[[nodiscard]] constexpr bool near_dead(const NearUnit& unit) noexcept {
    if (unit.dead_flag) return true;
    if (unit.type == 0) return unit.mode == 0 || unit.mode == 0x11;
    return unit.type == 1 && (unit.mode == 0 || unit.mode == 0xc);
}

// FUN_006417f0, the AI distance (monsters.hpp's point_apart, less 0):
// (max x 2 + min) / 2 of the axis gaps.
[[nodiscard]] constexpr int near_apart(int dx, int dy) noexcept {
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    return (dx <= dy ? dx + dy * 2 : dy + dx * 2) / 2;
}

// What the unit finder (FUN_0065a950 / FUN_0065ac70) gives FUN_00467490.
// Iron Golem (skill 0x5a): its filter LAB_00467470, an item Items bitfield1
// & 2, so any distance and a town too. Else FUN_0065aa40 with mask 0x2003
// (players, monsters, rooms not a town's), or 0x3002 for a TargetCorpse skill
// (monsters, only the dead, mode 0xc): within 9 of the spot (dx² + dy² <= 81),
// a player not in mode 0 / 0x11 and not the local player, a monster not in 0
// / 0xc. The town is FUN_0061ab00 (levels 1, 40, 75, 103, 109).
[[nodiscard]] constexpr bool near_found(const NearUnit& unit, bool self, bool target_corpse, bool iron_golem, bool town, int spot_x, int spot_y) noexcept {
    if (iron_golem) return unit.type == 4 && unit.metal;
    if (town) return false;
    const int dx = unit.x - spot_x, dy = unit.y - spot_y;
    if (dx * dx + dy * dy > 81) return false;
    if (unit.type == 0) return !target_corpse && unit.mode != 0 && unit.mode != 0x11 && !self;
    if (unit.type != 1) return false;
    return target_corpse ? unit.mode == 0xc : unit.mode != 0 && unit.mode != 0xc;
}

// FUN_00467490 (SearchEnemyNear: Raise Skeleton, Corpse Explosion, Iron Golem,
// Psychic Hammer, Fist of the Heavens, Taunt...): the click's target (-1 none)
// and spot in, the target it leaves out. A dead target of a TargetCorpse skill
// stays, spot and all. Else a target moves the spot to its subtile; Iron
// Golem keeps a target metal item. Then the unit the finder gives (`units` in
// its walk order, `self` the local player) nearest the spot by FUN_006417f0
// under 10 that the hit test passes, the first of equals; none leaves the
// target as it was.
[[nodiscard]] constexpr int search_enemy_near(bool target_corpse, bool iron_golem, bool town, int self, int target, int& spot_x, int& spot_y,
                                              std::span<const NearUnit> units) noexcept {
    if (target >= 0) {
        const auto& aimed = units[std::size_t(target)];
        if (target_corpse && near_dead(aimed)) return target;
        spot_x = aimed.x;
        spot_y = aimed.y;
        if (iron_golem && aimed.type == 4 && aimed.metal) return target;
    }
    int best = target, best_apart = 10;
    for (std::size_t index = 0; index < units.size(); ++index) {
        const auto& unit = units[index];
        if (!near_found(unit, int(index) == self, target_corpse, iron_golem, town, spot_x, spot_y)) continue;
        const int apart = near_apart(spot_x - unit.x, spot_y - unit.y);
        if (unit.hit && apart < best_apart) {
            best = int(index);
            best_apart = apart;
        }
    }
    return best;
}

}  // namespace d2d::rules
