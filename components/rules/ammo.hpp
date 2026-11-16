// SPDX-License-Identifier: GPL-3.0-or-later
// Ammunition: whether a stack has any left (FUN_0062a310).
// docs/research/re/skills.md "Shots and ammo". Checked against
// game.exe's own code by tools/emu/items_traces2.py.
#pragma once

#include "replenish.hpp"
#include "rules.hpp"

#include <d2s_items.hpp>

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

}  // namespace d2d::rules
