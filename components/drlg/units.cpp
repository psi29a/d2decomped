// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for units.hpp.
#include "units.hpp"

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

std::vector<Unit> ds1_units(const d2d::ds1::Map& map, const UnitIds& ids) {
    std::vector<Unit> list;
    const int version = map.version(), act = std::clamp(int(map.act()) - 1, 0, 4);
    for (const auto& object : map.objects()) {
        Unit unit{ object.type, object.id, 0, object.x, object.y, version > 5 ? std::uint32_t(object.flags) : 0u };
        for (const auto& point : object.path) {
            unit.path.emplace_back(point.x - object.x, point.y - object.y);
            unit.actions.push_back(point.action);
        }
        if (unit.type == unit_type::kMonster) {
            if (version < 5) continue;
            unit.mode = 1;
            const auto& table = ids.monpreset[std::size_t(act)];
            if (unit.id >= 0 && unit.id < int(table.size())) {
                const auto [kind, id] = table[std::size_t(unit.id)];
                unit.id = kind == 0 ? id + ids.monstats + ids.superuniques : kind == 1 ? id : kind == 2 ? id + ids.monstats : -1;
            }
            // Monsters that are objects (FUN_00665950 by act): Act 3's 0x129 /
            // 0x16e objects 0x17e / 0x194, Act 5's 0x202 object 0x1cd and
            // 0x219..0x21b 0x3f5 less the id.
            const int as_object = unit.id < 0 || unit.id >= ids.monstats ? -1
                                : act == 2 ? (unit.id == 0x129 ? 0x17e : unit.id == 0x16e ? 0x194 : -1)
                                : act == 4 ? (unit.id == 0x202 ? 0x1cd : unit.id >= 0x219 && unit.id <= 0x21b ? 0x3f5 - unit.id : -1) : -1;
            if (as_object >= 0) { unit.type = unit_type::kObject; unit.id = as_object; unit.mode = 0; }
        } else if (unit.type == unit_type::kObject) {
            if (version < 6) { if (unit.id == 0x23d) unit.id = -1; }
            else unit.id = unit.id < 150 ? int(kObjPreset[std::size_t(act)][std::size_t(unit.id)]) : unit.id - 150;
        }
        if (unit.id < 0) continue;
        list.insert(list.begin(), unit);
    }
    return list;
}

bool rolled_unit(const Unit& unit, const UnitIds& ids) {
    if (unit.type == unit_type::kMonster) {
        if (unit.id < ids.monstats) return unit.id == 0xcc || unit.id == 0xcd || unit.id == 0x173 || unit.id == 0x174;
        const int place = unit.id - ids.monstats - ids.superuniques;
        return place == 0x21 || place == 0x22 || place == 0x23;
    }
    return unit.type == unit_type::kObject && (unit.id == 0xc4 || unit.id == 0x105 || unit.id == 0x245);
}

}  // namespace d2d::drlg
