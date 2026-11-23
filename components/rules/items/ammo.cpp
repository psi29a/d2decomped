// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of ammo.hpp's functions; their notes are in the header.
#include "ammo.hpp"

#include "item_price.hpp"
#include "replenish.hpp"
#include "rules.hpp"

#include <d2s_items.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace d2d::rules {

bool quantity_left(const Tables& tables, const d2d::d2s::Item& item) {
    return quantity_left(item.quantity, own_stat(tables, item, kItemThrowable) != 0, stack_most(tables, item));
}

ShotSpent shot_spent(int quantity, int durability_most) {
    const auto next = std::int32_t(std::uint32_t(quantity) - 1u);
    if (next < 0) return { 0, durability_most, 0 };
    return { next, durability_most, std::int32_t(std::uint32_t(next) + 1u) };
}

Spent spent_fate(const SpentItem& item) {
    if ((!item.stackable && !item.throwable && !item.throw_stat) || item.quantity > 0) return {};
    if (item.throw_stat) return { SpentFate::emptied };
    if (item.weapon && item.magic) return { item.broken ? SpentFate::stays : SpentFate::breaks };
    return { SpentFate::leaves, item.reload, item.reequip };
}

int reload_pick(std::span<const PackItem> pack) {
    for (std::size_t at = 0; at < pack.size(); ++at)
        if (pack[at].same_class && !pack[at].broken) return int(at);
    return -1;
}

int reequip_slot(const KeptWeapon& kept) {
    if (kept.id == 0 || !kept.of_game || kept.page != 0 || !kept.ours) return -1;
    return kept.found_slot;
}

}
