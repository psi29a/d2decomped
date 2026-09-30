// Preset units (.\DRLG\Preset.cpp): the monsters, objects and warps a
// level's rooms hold for the server to spawn — from the preset DS1s
// (FUN_00665950, FUN_00667620), LvlSub stamps (FUN_0066fa10), a few
// hard-coded ones (FUN_006664a0: Flavie) and warp tiles (FUN_0066e1c0).
// docs/research/re/drlg.md "Preset units".
#pragma once

#include "obj_preset.hpp"

#include <ds1.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace d2d::drlg {

// A unit as a room lists it (FUN_0066bf30): type 1 monster (MonStats row;
// row count + i superunique i; + superunique count + i MonPlace i), 2
// object (objects.txt row), 5 warp (LvlWarp Id); mode (monsters 1);
// subtiles; DS1 flags.
struct Unit { int type = 0, id = 0, mode = 0, x = 0, y = 0; std::uint32_t flags = 0; };

// What a DS1's ids map through: MonPreset by act ({kind, id}: 0 MonPlace,
// 1 MonStats, 2 SuperUniques) and the table sizes the ids offset by.
struct UnitIds {
    std::array<std::vector<std::pair<int, int>>, 5> monpreset;
    int monstats = 0, superuniques = 0;
};


// A DS1's units as its loader lists them (FUN_00665950): mapped ids, each
// put at the front, so the list runs from the file's last object back.
// Monsters need v5+; ids that map to nothing are dropped.
// ponytail: the act 2 / act 4 monster remaps and type 4 (NPCs by name)
// aren't there — act 1's levels don't hit them.
inline std::vector<Unit> ds1_units(const d2d::ds1::Map& map, const UnitIds& ids) {
    std::vector<Unit> list;
    const int version = map.version(), act = std::clamp(int(map.act()) - 1, 0, 4);
    for (const auto& object : map.objects()) {
        Unit unit{ object.type, object.id, 0, object.x, object.y, version > 5 ? std::uint32_t(object.flags) : 0u };
        if (unit.type == 1) {
            if (version < 5) continue;
            unit.mode = 1;
            const auto& table = ids.monpreset[std::size_t(act)];
            if (unit.id >= 0 && unit.id < int(table.size())) {
                const auto [kind, id] = table[std::size_t(unit.id)];
                unit.id = kind == 0 ? id + ids.monstats + ids.superuniques : kind == 1 ? id : kind == 2 ? id + ids.monstats : -1;
            }
        } else if (unit.type == 2) {
            if (version < 6) { if (unit.id == 0x23d) unit.id = -1; }
            else unit.id = unit.id < 150 ? int(kObjPreset[std::size_t(act)][std::size_t(unit.id)]) : unit.id - 150;
        }
        if (unit.id < 0) continue;
        list.insert(list.begin(), unit);
    }
    return list;
}

// FUN_00667620's drops: these ids survive a roll (1 in 3 / 4 / 2); act 1's
// Cottages 2 (Cott4A.ds1) carries one.
inline bool rolled_unit(const Unit& unit, const UnitIds& ids) {
    if (unit.type == 1) {
        if (unit.id < ids.monstats) return unit.id == 0xcc || unit.id == 0xcd || unit.id == 0x173 || unit.id == 0x174;
        const int place = unit.id - ids.monstats - ids.superuniques;
        return place == 0x21 || place == 0x22 || place == 0x23;
    }
    return unit.type == 2 && (unit.id == 0xc4 || unit.id == 0x105 || unit.id == 0x245);
}

// FUN_00667620's roll for one of those: a step of `seed` (outdoors the
// level's for a preset with Scan or Pops, else the room's), kept on its low
// word — monsters 1 in 3, MonPlace group25 3 in 4, group50 1 in 2, group75 1
// in 4 (group100 never rolls), objects 0xc4 / 0x105 1 in 2, 0x245 3 in 4.
// `Seed` is d2d::rules::Rng.
template <class Seed>
bool stays(const Unit& unit, const UnitIds& ids, Seed& seed) {
    if (!rolled_unit(unit, ids)) return true;
    const std::uint32_t low = seed.next();
    if (unit.type == 1 && unit.id < ids.monstats) return low % 3 == 0;
    if (unit.type == 1) {
        const int place = unit.id - ids.monstats - ids.superuniques;
        return place == 0x21 ? (low & 3) != 0 : place == 0x22 ? (low & 1) != 0 : (low & 3) == 0;
    }
    return unit.id == 0x245 ? (low & 3) != 0 : (low & 1) == 0;
}

}  // namespace d2d::drlg
