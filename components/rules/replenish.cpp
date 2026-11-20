// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of replenish.hpp's functions; their notes are in the header.
#include "replenish.hpp"

#include "rules.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>

namespace d2d::rules {

std::optional<int> replenish_first(int rate, int timer) {
    if (rate == 0 || timer != 0) return std::nullopt;
    return kReplenishFrames / rate + 1;
}

// A point that leaves the value at 1 or more mends a broken item whose
// owner is told (0x562cbd .. 0x562ce2).
bool replenish_point(int rate, int& value, int most, bool broken, bool owned, Replenished& out) {
    if (rate == 0) return false;
    const int next = int(std::uint32_t(value) + 1u);   // add esi, 1: wraps
    if (next > most) return true;
    value = next;
    if (owned && next >= 1 && broken) out.mend = true;
    out.wait = std::max(kReplenishFrames / rate + 1, kReplenishLeast);
    return true;
}

// An unbroken item with durability takes a durability point; when its
// rate (stat 0xfc) is set that is all, at its max too. Else a stackable
// item takes one of quantity.
Replenished replenish_step(const Replenishing& item) {
    Replenished out{ item.durability, item.quantity, std::nullopt, false };
    if (!item.broken && item.wears
        && replenish_point(item.durability_rate, out.durability, item.durability_most, item.broken, item.owned, out))
        return out;
    if (item.stacks) (void)replenish_point(item.quantity_rate, out.quantity, item.quantity_most, item.broken, item.owned, out);
    return out;
}

int own_stat(const Tables& tables, const d2d::d2s::Item& item, int stat) {
    std::uint32_t total = 0;
    for (const auto& prop : item.props) if (prop.stat == stat) total += std::uint32_t(prop.value);
    for (const auto& filled : item.socketed_items)
        for (const auto& prop : socket_props(tables, item, filled)) if (prop.stat == stat) total += std::uint32_t(prop.value);
    return int(total);
}

void replenish_arm(const Tables& tables, d2d::d2s::Item& item, int stat) {
    if (const auto wait = replenish_first(own_stat(tables, item, stat), item.replenish_in)) item.replenish_in = std::max(*wait, 1);
}

void replenish_arm(const Tables& tables, d2d::d2s::Item& item) {
    replenish_arm(tables, item, kReplenishDurability);
    replenish_arm(tables, item, kReplenishQuantity);
}

// True when the point mended it (FUN_0055f900: flag 0x200 on and 0x100
// off, durability back to its max when it has one); its stats then go
// back on (wield_enters, settle_requirements). FUN_0055f900 ends with
// the hand-skill pass FUN_0055c5c0, which needs the character's skills:
// World::hand_skills, run by replenish_frame on a mend, as by a repair.
bool replenish_fire(const Tables& tables, d2d::d2s::Item& item, bool owned) {
    const auto base = tables.item_base.find(item.code);
    const Replenishing now{ broken(item), has_durability(tables, item), base != tables.item_base.end() && base->second.stackable, owned,
                            own_stat(tables, item, kReplenishDurability), item.durability, max_durability(item),
                            own_stat(tables, item, kReplenishQuantity), std::max(item.quantity, 0), stack_most(tables, item) };
    const auto after = replenish_step(now);
    item.replenish_in = after.wait.value_or(0);
    if (after.durability != now.durability) item.durability = after.durability;
    if (after.quantity != now.quantity) item.quantity = after.quantity;
    if (!after.mend) return false;
    item.flags = (item.flags | kItemRepaired) & ~0x100u;
    if (const int most = max_durability(item); most > 0) item.durability = most;
    return true;
}

}
