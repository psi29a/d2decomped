// Preset units (.\DRLG\Preset.cpp): the monsters, objects and warps a
// level's rooms hold for the server to spawn — from the preset DS1s
// (FUN_00665950, FUN_00667620), LvlSub stamps (FUN_0066fa10), a few
// hard-coded ones (FUN_006664a0: Flavie) and warp tiles (FUN_0066e1c0).
// docs/research/re/drlg.md "Preset units".
#pragma once

#include <ds1.hpp>
#include <obj_preset.hpp>

#include <algorithm>
#include <array>
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
inline std::vector<Unit> ds1_units(const d2d::ds1::Map& m, const UnitIds& ids) {
    std::vector<Unit> list;
    const int v = m.version(), act = std::clamp(int(m.act()) - 1, 0, 4);
    for (const auto& o : m.objects()) {
        Unit u{ o.type, o.id, 0, o.x, o.y, v > 5 ? std::uint32_t(o.flags) : 0u };
        if (u.type == 1) {
            if (v < 5) continue;
            u.mode = 1;
            const auto& t = ids.monpreset[std::size_t(act)];
            if (u.id >= 0 && u.id < int(t.size())) {
                const auto [kind, id] = t[std::size_t(u.id)];
                u.id = kind == 0 ? id + ids.monstats + ids.superuniques : kind == 1 ? id : kind == 2 ? id + ids.monstats : -1;
            }
        } else if (u.type == 2) {
            if (v < 6) { if (u.id == 0x23d) u.id = -1; }
            else u.id = u.id < 150 ? int(kObjPreset[std::size_t(act)][std::size_t(u.id)]) : u.id - 150;
        }
        if (u.id < 0) continue;
        list.insert(list.begin(), u);
    }
    return list;
}

// FUN_00667620's drops: these ids survive a roll (1 in 3 / 4 / 2); none of
// act 1's outdoor or cave presets carry them.
inline bool rolled_unit(const Unit& u, const UnitIds& ids) {
    if (u.type == 1) {
        if (u.id < ids.monstats) return u.id == 0xcc || u.id == 0xcd || u.id == 0x173 || u.id == 0x174;
        const int place = u.id - ids.monstats - ids.superuniques;
        return place == 0x21 || place == 0x22 || place == 0x23;
    }
    return u.type == 2 && (u.id == 0xc4 || u.id == 0x105 || u.id == 0x245);
}

}  // namespace d2d::drlg
