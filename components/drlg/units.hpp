// SPDX-License-Identifier: GPL-3.0-or-later
// Preset units (.\DRLG\Preset.cpp): the monsters, objects and warps a
// level's rooms hold for the server to spawn — from the preset DS1s
// (FUN_00665950, FUN_00667620), LvlSub stamps (FUN_0066fa10), a few
// hard-coded ones (FUN_006664a0: Flavie) and warp tiles (FUN_0066e1c0).
// docs/research/re/drlg.md "Preset units".
#pragma once

#include "obj_preset.hpp"

#include <ds1.hpp>
#include <object_ids.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace d2d::drlg {

namespace unit_type = d2d::rules::unit_type;

// A unit as a room lists it (FUN_0066bf30): type 1 monster (MonStats row;
// row count + i superunique i; + superunique count + i MonPlace i), 2
// object (objects.txt row), 5 warp (LvlWarp Id); mode (monsters 1);
// subtiles; DS1 flags; its DS1 path (v14+) as the unit's map AI
// (FUN_00665950 +0x10, moved with it by FUN_00667510), subtiles off it.
// path: offsets from the unit, and each point's action (an NPC's on arrival).
struct Unit { int type = 0, id = 0, mode = 0, x = 0, y = 0; std::uint32_t flags = 0; std::vector<std::pair<int, int>> path = {}; std::vector<int> actions = {}; };

// What a DS1's ids map through: MonPreset by act ({kind, id}: 0 MonPlace,
// 1 MonStats, 2 SuperUniques) and the table sizes the ids offset by.
struct UnitIds {
    std::array<std::vector<std::pair<int, int>>, 5> monpreset;
    int monstats = 0, superuniques = 0;
};


// A DS1's units as its loader lists them (FUN_00665950): mapped ids, each
// put at the front, so the list runs from the file's last object back.
// Monsters need v5+; ids that map to nothing are dropped.
// Type 4 (a unit by its code, v5+: FUN_00665950 reads the name table at
// 0x748ad4) is in none of 1.14d's DS1s: the 2,043 LvlPrest File1..6 maps
// carry only types 1 and 2, so it isn't mapped here.
std::vector<Unit> ds1_units(const d2d::ds1::Map& map, const UnitIds& ids);

// FUN_00667620's drops: these ids survive a roll (1 in 3 / 4 / 2); act 1's
// Cottages 2 (Cott4A.ds1) carries one.
bool rolled_unit(const Unit& unit, const UnitIds& ids);

// FUN_00667620's roll for one of those: a step of `seed` (outdoors the
// level's for a preset with Scan or Pops, else the room's), kept on its low
// word — monsters 1 in 3, MonPlace group25 3 in 4, group50 1 in 2, group75 1
// in 4 (group100 never rolls), objects 0xc4 / 0x105 1 in 2, 0x245 3 in 4.
// `Seed` is d2d::rules::Rng.
template <class Seed>
bool stays(const Unit& unit, const UnitIds& ids, Seed& seed) {
    if (!rolled_unit(unit, ids)) return true;
    const std::uint32_t low = seed.next();
    if (unit.type == unit_type::kMonster && unit.id < ids.monstats) return low % 3 == 0;
    if (unit.type == unit_type::kMonster) {
        const int place = unit.id - ids.monstats - ids.superuniques;
        return place == 0x21 ? (low & 3) != 0 : place == 0x22 ? (low & 1) != 0 : (low & 3) == 0;
    }
    return unit.id == 0x245 ? (low & 3) != 0 : (low & 1) == 0;
}

}  // namespace d2d::drlg
