// SPDX-License-Identifier: GPL-3.0-or-later
// An item that replenishes itself: item_replenish_durability (stat 0xfc,
// "Repairs 1 durability in N seconds") and item_replenish_quantity (stat
// 0xfd, "Replenishes quantity"). game.exe gives the item unit one timer,
// kind 3 (docs/research/re/items.md "The replenish timer"):
// - FUN_00558530 / FUN_00558580 queue it, 2500 / rate + 1 frames on, when
//   the stat is set and the item has no timer 3 yet (FUN_005415a0);
// - the timer runs FUN_00562d30 (through FUN_00562da0's table at 0x6e117c,
//   entry 3), which adds a point with FUN_00562c40 and queues it again.
// Checked against game.exe's own code by tools/emu/replenish.py
// (tests/test_replenish.cpp).
#pragma once

#include "rules.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>

namespace d2d::rules {

inline constexpr int kReplenishFrames = 2500;   // 0x9c4: a rate of 1 is a point in 100 seconds
inline constexpr int kReplenishLeast = 125;     // 0x7d: FUN_00562c40's next point is 5 seconds off at least

// FUN_00558530 / FUN_00558580: the frames to the item's first timer 3,
// 2500 / rate + 1 (a 32-bit idiv, toward zero), or none when the rate is 0
// or a timer 3 is queued already (`timer`: FUN_005415a0's answer, the
// frame of the one queued, 0 none).
[[nodiscard]] inline std::optional<int> replenish_first(int rate, int timer) {
    if (rate == 0 || timer != 0) return std::nullopt;
    return kReplenishFrames / rate + 1;
}

// What timer 3 reads of its item, and what it leaves.
struct Replenishing {
    bool broken = false;          // item flag 0x100 (FUN_006280a0)
    bool wears = false;           // FUN_00629930: it has durability
    bool stacks = false;          // FUN_006289f0: Items stackable
    bool owned = false;           // its owner (item data +0xc, FUN_00629f20) is a player in the game with a client
    int durability_rate = 0, durability = 0, durability_most = 0;   // stats 0xfc, 0x48, FUN_00625e00
    int quantity_rate = 0, quantity = 0, quantity_most = 0;         // stats 0xfd, 0x46, FUN_006295b0
};
struct Replenished {
    int durability = 0, quantity = 0;
    std::optional<int> wait;      // the frames to the next timer 3, none when it isn't queued again
    bool mend = false;            // the item is mended (FUN_0055f900): no longer broken
    friend bool operator==(const Replenished&, const Replenished&) = default;
};

// FUN_00562c40: a point of `value` up to `most`, false when the rate is 0.
// Past `most` nothing happens and the timer isn't queued again. A point
// that leaves the value at 1 or more mends a broken item whose owner is
// told (0x562cbd .. 0x562ce2). The next timer is 2500 / rate + 1 frames
// on, 125 at least.
inline bool replenish_point(int rate, int& value, int most, bool broken, bool owned, Replenished& out) {
    if (rate == 0) return false;
    const int next = int(std::uint32_t(value) + 1u);   // add esi, 1: wraps
    if (next > most) return true;
    value = next;
    if (owned && next >= 1 && broken) out.mend = true;
    out.wait = std::max(kReplenishFrames / rate + 1, kReplenishLeast);
    return true;
}

// Timer 3 (FUN_00562d30). An unbroken item with durability takes a
// durability point; when its rate (stat 0xfc) is set that is all, at its
// max too. Else a stackable item takes one of quantity.
[[nodiscard]] inline Replenished replenish_step(const Replenishing& item) {
    Replenished out{ item.durability, item.quantity, std::nullopt, false };
    if (!item.broken && item.wears
        && replenish_point(item.durability_rate, out.durability, item.durability_most, item.broken, item.owned, out))
        return out;
    if (item.stacks) (void)replenish_point(item.quantity_rate, out.quantity, item.quantity_most, item.broken, item.owned, out);
    return out;
}

// The item's own stat (FUN_00625480 on the item unit): its lists, what
// its sockets hang on them included.
[[nodiscard]] inline int own_stat(const Tables& tables, const d2d::d2s::Item& item, int stat) {
    std::uint32_t total = 0;
    for (const auto& prop : item.props) if (prop.stat == stat) total += std::uint32_t(prop.value);
    for (const auto& filled : item.socketed_items)
        for (const auto& prop : socket_props(tables, item, filled)) if (prop.stat == stat) total += std::uint32_t(prop.value);
    return int(total);
}

// FUN_00558530 (`stat` kReplenishDurability) / FUN_00558580
// (kReplenishQuantity): queues the item's timer 3. A frame that isn't
// after the game's becomes the next one (FUN_005416b0, 0x5416e9).
inline void replenish_arm(const Tables& tables, d2d::d2s::Item& item, int stat) {
    if (const auto wait = replenish_first(own_stat(tables, item, stat), item.replenish_in)) item.replenish_in = std::max(*wait, 1);
}
// Both, the durability one first: a made or loaded item (FUN_00558d90,
// FUN_00558cb0), a socket filled (FUN_00562660), one back off a corpse
// (FUN_00562f30).
inline void replenish_arm(const Tables& tables, d2d::d2s::Item& item) {
    replenish_arm(tables, item, kReplenishDurability);
    replenish_arm(tables, item, kReplenishQuantity);
}

// The item's timer 3 has come (FUN_00562d30). True when the point mended
// it (FUN_0055f900: flag 0x200 on and 0x100 off, durability back to its max
// when it has one); its stats then go back on (wield_enters,
// settle_requirements). FUN_0055f900 ends with the hand-skill pass
// FUN_0055c5c0, which needs the character's skills: World::hand_skills,
// run by replenish_frame on a mend, as by a repair.
inline bool replenish_fire(const Tables& tables, d2d::d2s::Item& item, bool owned) {
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

}  // namespace d2d::rules
