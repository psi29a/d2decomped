// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of hand_pick.hpp's functions; their notes are in the header.
#include "hand_pick.hpp"

#include "item_price.hpp"
#include "rules.hpp"
#include "wear.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::rules {

// FUN_00629fe0 looks the item record's +0xc0 (weapons.txt wclass) up in the eight pairs at 0x7446a0
// (combat.md "the weapon in hand"); a missing key answers 0.
int wclass_key(std::string_view wclass) {
    static constexpr std::array<std::pair<std::string_view, int>, 8> kKeys = { {
        { "bow", 1 }, { "1hs", 2 }, { "1ht", 3 }, { "stf", 4 }, { "2hs", 5 }, { "2ht", 6 }, { "xbw", 7 }, { "ht1", 12 } } };
    for (const auto& [name, key] : kKeys)
        if (name == wclass) return key;
    return 0;
}

// The body grid's cells 1..10 (FUN_0063add0 grid 0), each item's type tests (FUN_00629bb0: its type, then its
// type2), FUN_00629fe0's key, FUN_00628660's component, FUN_0062a4e0, and FUN_0063bef0's wielded weapon.
std::vector<WornItem> worn_items(const Tables& tables, const std::vector<d2d::d2s::Item>& items) {
    std::vector<WornItem> worn;
    const auto* wielded = wielded_weapon(tables, items);
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        if (item.location != d2d::d2s::item_location::kEquipped || item.slot < d2d::d2s::body_location::kFirst
            || item.slot > d2d::d2s::body_location::kLast) continue;
        WornItem row{ .cell = item.slot, .usable = item_usable(item), .wielded = &item == wielded, .index = int(index) };
        const auto* info = info_of(tables, item);
        const auto base = tables.item_base.find(item.code);
        const std::string none;
        const auto& type = info ? info->type : none;
        const auto& type2 = base != tables.item_base.end() ? base->second.type2 : none;
        row.melee = type_is(tables, type, "mele") || type_is(tables, type2, "mele");
        row.weapon = type_is(tables, type, "weap") || type_is(tables, type2, "weap");
        if (base != tables.item_base.end()) {
            row.class_key = wclass_key(base->second.wclass);
            row.component = base->second.component;
        }
        worn.push_back(row);
    }
    std::ranges::sort(worn, {}, &WornItem::cell);
    return worn;
}

// FUN_0063c050 walks cells 0..10: a one-hand swung or thrust weapon or a claw (key 2, 3, 12) is component 5
// when it is the wielded weapon, 6 when it isn't (whichever hand holds it); any other item by its component.
const WornItem* worn_by_component(std::span<const WornItem> worn, int component) {
    for (const auto& row : worn) {
        if (row.class_key == 2 || row.class_key == 3 || row.class_key == 12) {
            if ((component == 6 && !row.wielded) || (component == 5 && row.wielded)) return &row;
        } else if (row.component == component) {
            return &row;
        }
    }
    return nullptr;
}

// FUN_005d92d0 (unit in EAX): FUN_0063c050's 6 and 5 both ItemTypes 0x2e; no usable test (FUN_0062a4e0).
bool two_melee(std::span<const WornItem> worn) {
    const auto* left = worn_by_component(worn, 6);
    const auto* right = worn_by_component(worn, 5);
    return left && left->melee && right && right->melee;
}

// FUN_00535bc0 (ECX unit): the default is FUN_0063c9b0's, cell 4's then cell 5's usable weapon (0x2d). A two-handed
// class (FUN_006235a0) with weapsel 2 takes component 5's item, or component 6's when 5's is none or no weapon, or
// when 6's is a weapon and the node's flags (FUN_006446a0) have 0x2000; that one only when a usable weapon.
const WornItem* weapsel2_pick(std::span<const WornItem> worn, bool two_handed_class, bool left_flag) {
    const WornItem* fallback = nullptr;
    for (const int cell : { d2d::d2s::body_location::kRightArm, d2d::d2s::body_location::kLeftArm }) {
        const auto found = std::ranges::find(worn, cell, &WornItem::cell);
        if (!fallback && found != worn.end() && found->weapon && found->usable) fallback = &*found;
    }
    if (!two_handed_class) return fallback;
    const auto* right = worn_by_component(worn, 5);
    const auto* left = worn_by_component(worn, 6);
    const auto* pick = (!right || !right->weapon || (left && left->weapon && left_flag)) ? left : right;
    return pick && pick->weapon && pick->usable ? pick : fallback;
}

}  // namespace d2d::rules
