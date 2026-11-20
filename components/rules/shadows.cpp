// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of shadows.hpp's functions; their notes are in the header.
#include "shadows.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace d2d::rules {

bool shadow_may_have(int summon, int pet_type, int pet_class, int own_pet_type, int pet_types) {
    if (summon == 0) return true;
    if (summon == (pet_class & 0xffff)) return false;
    if (pet_type >= 0 && pet_type < pet_types) return own_pet_type != pet_type;
    return true;
}

bool shadow_ai_may_use(bool may_have, int aitype, bool melee, bool target, bool in_state, bool target_in_state2, bool progressive,
                       std::optional<int> charges) {
    if (!may_have || melee != (aitype == 4 || aitype == 13)) return false;
    if (aitype == 1 && in_state) return false;
    if (!target && (aitype == 2 || aitype == 4 || aitype == 5 || aitype == 11 || aitype == 12 || aitype == 13)) return false;
    if (aitype == 2 && (in_state || target_in_state2)) return false;
    return !(progressive && in_state && charges && *charges >= 3);
}

bool shadow_worth(bool monster, bool dying, bool npc, bool killable, bool boss, bool prime_evil, bool special) {
    return monster && !dying && !npc && killable && (boss || prime_evil || special);
}

int shadow_distance(const ShadowUnit& from, const ShadowUnit& unit) {   // FUN_005b0bd0
    return (from.x - unit.x) * (from.x - unit.x) + (from.y - unit.y) * (from.y - unit.y);
}

ShadowScan shadow_scan(std::span<const ShadowUnit> units, int pet, int owner) {
    ShadowScan scan;
    for (int index = 0; index < int(units.size()); ++index) {
        const auto& unit = units[std::size_t(index)];
        if (index == pet || unit.dying) continue;
        if (unit.type == 1 && unit.side && unit.monster_id >= 0x19a && unit.monster_id <= 0x1a0 && unit.monster_id != 0x19e) { ++scan.traps; continue; }
        if (!unit.targetable || !unit.foe) continue;
        if (owner >= 0) {
            const int apart = shadow_distance(units[std::size_t(owner)], unit);
            if (apart <= 0x64) ++scan.owner_close_count;
            if (apart < scan.owner_closest_distance) { scan.owner_closest = index; scan.owner_closest_distance = apart; }
        }
        const int apart = shadow_distance(units[std::size_t(pet)], unit);
        if (apart > 0x400) continue;
        ++scan.all;
        if (apart <= 0x64) ++scan.close_count;
        if (apart < scan.closest_distance) { scan.closest = index; scan.closest_distance = apart; }
        if (unit.worth) scan.worth = index;
    }
    return scan;
}

}
