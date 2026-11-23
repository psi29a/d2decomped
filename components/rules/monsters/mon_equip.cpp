// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of mon_equip.hpp's functions; their notes are in the header.
#include "mon_equip.hpp"

#include "rules.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace d2d::rules {

// A "    " code with an owner takes the owner's item in that slot (skipped
// when it has none); without one it goes to FUN_00573b20 as "    ", which
// makes nothing. A unit without an inventory (+0x60) gets nothing: the
// caller's check.
std::vector<MonGear> monster_gear(std::span<const MonEquipRow> rows, int first, int monster, int level, bool oninit, std::uint32_t filled,
                                  std::optional<std::uint32_t> owner, Rng& own) {
    std::vector<MonGear> out;
    if (first < 0 || std::size_t(first) >= rows.size()) return out;
    std::size_t row = std::size_t(first);
    if (oninit && !rows[row].oninit) return out;
    while (row < rows.size() && rows[row].monster == monster && rows[row].level > level) ++row;
    for (; row < rows.size() && rows[row].monster == monster; ++row) {
        const auto& equip = rows[row];
        int count = 0;
        while (count < 3 && equip.loc[std::size_t(count)] > 0 && equip.loc[std::size_t(count)] < 11) ++count;
        if (count == 0) continue;
        const auto slot = std::size_t(own(count));
        const int loc = equip.loc[slot];
        if (filled & (1u << loc)) continue;
        const bool owners = equip.item[slot] == kOwnersItem && owner;
        if (owners && !(*owner & (1u << loc))) continue;
        const int quality = equip.quality[slot] < 0 || equip.quality[slot] > 7 ? 0 : equip.quality[slot];
        out.push_back({ row, slot, loc, quality, owners });
        if (owners || equip.item[slot] != kOwnersItem) filled |= 1u << loc;
    }
    return out;
}

int summon_gear_level(int calc2, int skill_level, int owner_level) {
    if (calc2 != 0) return calc2;
    return std::min(std::max(3 * skill_level, 1), owner_level);
}

GearSum gear_sum(const Tables& tables, std::span<const d2d::d2s::Item> items) {
    using namespace d2d::d2s;
    GearSum gear;
    for (const auto& item : items) {
        std::int64_t enhanced_defense = 0, enhanced_min = 0, enhanced_max = 0;
        for (const auto& prop : item.props) {
            if (prop.stat == kArmorPercent) enhanced_defense += prop.value;
            else if (prop.stat == kMinDamagePercent) enhanced_min += prop.value;
            else if (prop.stat == kMaxDamagePercent) enhanced_max += prop.value;
            else if (prop.stat >= 0 && std::size_t(prop.stat) < gear.sum.size()) gear.sum[std::size_t(prop.stat)] += prop.value;
        }
        if (item.defense > 0) gear.defense += item.defense * (100 + enhanced_defense) / 100;
        if (item.slot != body_location::kRightArm && item.slot != body_location::kLeftArm) continue;
        const auto info = tables.item_info.find(item.code);
        const auto base = tables.item_base.find(item.code);
        if (gear.weapon || info == tables.item_info.end() || base == tables.item_base.end() || info->second.kind != 2) continue;
        gear.weapon = true;
        gear.two_handed = info->second.two_handed;
        gear.min = base->second.mindam * (100 + enhanced_min) / 100;
        gear.max = base->second.maxdam * (100 + enhanced_max) / 100;
    }
    return gear;
}

std::pair<int, int> gear_damage(const GearSum& gear, int mode_min, int mode_max) {
    using namespace d2d::d2s;
    const auto& sum = gear.sum;
    if (gear.two_handed) return { int(gear.min + sum[kSecondaryMinDamage]), int(gear.max + sum[kSecondaryMaxDamage]) };
    return { int(mode_min + gear.min + sum[kMinDamage]), int(mode_max + gear.max + sum[kMaxDamage]) };
}

}
