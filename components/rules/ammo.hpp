// SPDX-License-Identifier: GPL-3.0-or-later
// Ammunition: whether a stack has any left (FUN_0062a310) and what a
// hand's item does once it has none (FUN_00580030, .\PLAYER\PlrModes.cpp).
// docs/research/re/skills.md "Shots and ammo". Checked against
// game.exe's own code by tools/emu/items_traces2.py (FUN_0062a310) and
// tools/emu/ammo_out.py (FUN_00580030; tests/test_ammo_out.cpp).
#pragma once

#include "replenish.hpp"
#include "rules.hpp"

#include <d2s_items.hpp>

#include <cstddef>
#include <span>

namespace d2d::rules {

inline constexpr int kItemThrowable = 0x7d;   // item_throwable: no 1.14d item rolls it (stubs.md)

// FUN_0062a310: the ammunition has some left. Its quantity (stat 0x46)
// above 0; else an item with stat 0x7d always has (game.exe writes its
// quantity 0 there); else only one without a max stack (FUN_006295b0 at 0
// or less).
[[nodiscard]] inline bool quantity_left(int quantity, bool throw_stat, int most) { return quantity > 0 || throw_stat || most <= 0; }
[[nodiscard]] inline bool quantity_left(const Tables& tables, const d2d::d2s::Item& item) {
    return quantity_left(item.quantity, own_stat(tables, item, kItemThrowable) != 0, stack_most(tables, item));
}

// What FUN_00580030 reads of a hand's item.
struct SpentItem {
    bool stackable = false;      // FUN_006289f0: Items stackable
    bool throwable = false;      // FUN_0062ba80: its ItemTypes row's Throwable
    bool throw_stat = false;     // stat 0x7d set
    int quantity = 0;            // stat 0x46
    bool weapon = false;         // ItemTypes weap (0x2d)
    bool magic = false;          // FUN_0062a0f0: quality 4..9
    bool broken = false;         // item flag 0x100
    bool reload = false;         // its ItemTypes row's Reload (+0x11, FUN_0062e7e0)
    bool reequip = false;        // its ItemTypes row's ReEquip (+0x12, FUN_0062e830)
};
enum class SpentFate {
    stays,      // not ammunition, some left, or a magic weapon broken already
    emptied,    // stat 0x7d: its quantity is written 0 and it stays
    breaks,     // a magic or better weapon: FUN_0055f850
    leaves,     // off its slot (FUN_00560cd0)
};
// `reload`: the pack's first of its code, not broken, is put in its slot
// (reload_pick). `reequip`: when none took it, game.exe's ReEquip runs.
// ponytail: that step, FUN_0057ff70, isn't ported (Fight::out_of_ammo).
struct Spent {
    SpentFate fate = SpentFate::stays;
    bool reload = false, reequip = false;
    friend bool operator==(const Spent&, const Spent&) = default;
};

// FUN_00580030: a stackable or throwable item, or one with stat 0x7d, at
// quantity 0 or less.
[[nodiscard]] inline Spent spent_fate(const SpentItem& item) {
    if ((!item.stackable && !item.throwable && !item.throw_stat) || item.quantity > 0) return {};
    if (item.throw_stat) return { SpentFate::emptied };
    if (item.weapon && item.magic) return { item.broken ? SpentFate::stays : SpentFate::breaks };
    return { SpentFate::leaves, item.reload, item.reequip };
}

// The reload's pick among the pack's items of the spent one's type, in
// the pack's order (FUN_0063cd80): the first of its class (its code) that
// isn't broken, and no other; -1 none.
struct PackItem { bool same_class = false, broken = false; };
[[nodiscard]] inline int reload_pick(std::span<const PackItem> pack) {
    for (std::size_t at = 0; at < pack.size(); ++at)
        if (pack[at].same_class && !pack[at].broken) return int(at);
    return -1;
}

}  // namespace d2d::rules
