// SPDX-License-Identifier: GPL-3.0-or-later
// A set's bonus list on its owner (Sets.txt's partial and full bonuses):
// which rows it holds, and the set piece it was last built with, which the
// Properties funcs get as their item (dmg-min / dmg-max by that piece's
// hands). docs/research/re/combat.md "A set's bonus list". Checked against
// game.exe's own code by tools/emu/set_list.py (tests/test_set_list.cpp).
#pragma once

#include "rules.hpp"

#include <d2s_items.hpp>

#include <vector>

namespace d2d::rules {

// An item's stats go on (FUN_0055c2c0): a set piece rebuilds its set's
// list with itself (FUN_00663cc0 -> FUN_00663b40), so it takes the next
// stamp of `items`.
void stats_on(std::vector<d2d::d2s::Item>& items, d2d::d2s::Item& item);

// The requirements pass's last loop (FUN_0055dbc0): every worn set piece,
// neither broken nor flagged 0x4000, in body slot order, its stats off and on.
void restamp_worn_sets(std::vector<d2d::d2s::Item>& items);

// A set piece's break (FUN_0055f850) frees its set's list: the pieces still
// worn give no set bonus until one's stats go on again.
void free_set_list(const Tables& tables, std::vector<d2d::d2s::Item>& items, const d2d::d2s::Item& broken);

// A set row's member worn and counted (FUN_0062a370's mask): slots 1..10,
// a set item of a known row, flags 0x4000 / 0x100 off. Its Sets.txt row, or -1.
[[nodiscard]] int worn_set(const Tables& tables, const d2d::d2s::Item& item);

// The distinct members of Sets.txt row `set` worn (0x6eda40[mask]).
[[nodiscard]] int set_pieces_worn(const Tables& tables, const std::vector<d2d::d2s::Item>& items, int set);

// The worn member of `set` its list was last built with: the highest
// stamp, the later in `items` on a tie. nullptr when none is worn or the
// list was freed (free_set_list).
[[nodiscard]] const d2d::d2s::Item* set_list_piece(const Tables& tables, const std::vector<d2d::d2s::Item>& items, int set);

// FUN_00660120's rows with `count` members worn: partial i as i, full j as 8 + j.
[[nodiscard]] std::vector<int> set_list_rows(const SetBonus& bonus, int count);

}   // namespace d2d::rules
