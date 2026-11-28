// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The requirements pass's set lists against stock 1.14d, run by
// tools/emu/set_settle.py (FUN_0055dbc0 for real, every requirement met):
// which body slots' stats went on, in last-on order (rules::settle_requirements
// with its last loop, rules::restamp_worn_sets: the stamps). Then a set
// piece's break (FUN_0055f850 -> FUN_00663cc0(.., 1, 1) after the pass):
// rules::free_set_list leaves the set no list until a piece's stats go on.
// No game data needed.
#include <rules.hpp>
#include <set_list.hpp>
#include <wear.hpp>

#include <d2s_items.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <utility>
#include <vector>

namespace {

using namespace d2d::rules;

struct Case { int quality[10]; int flags[10]; int order[10]; };
constexpr Case kCases[] = {
#include "set_settle_cases.inc"
};

// Ten body slots' items, all wearable by anyone: a cap of the given quality and flags.
std::vector<d2d::d2s::Item> worn(const Case& one) {
    std::vector<d2d::d2s::Item> items;
    for (int slot = 0; slot < 10; ++slot) {
        if (one.quality[slot] < 0) continue;
        d2d::d2s::Item item;
        item.code = "cap";
        item.identified = true;
        item.quality = one.quality[slot];
        item.flags = std::uint32_t(one.flags[slot]);
        item.location = d2d::d2s::item_location::kEquipped;
        item.slot = d2d::d2s::body_location::kFirst + slot;
        items.push_back(std::move(item));
    }
    return items;
}

// The slots (0-based) settle_requirements stamped, by stamp.
std::vector<int> stamped(std::vector<d2d::d2s::Item>& items, const Tables& tables) {
    settle_requirements(tables, items, [] { return Wearer{ 0, 1000, 1000, 99 }; }, [](const d2d::d2s::Item&) { return -1; });
    std::vector<const d2d::d2s::Item*> stamped_items;
    for (const auto& item : items) if (item.set_stamp > 0) stamped_items.push_back(&item);
    std::ranges::sort(stamped_items, {}, &d2d::d2s::Item::set_stamp);
    std::vector<int> slots;
    for (const auto* item : stamped_items) slots.push_back(item->slot - d2d::d2s::body_location::kFirst);
    return slots;
}

}  // namespace

// The oracle's FUN_0055dbc0 cases, then a set piece's break and the next pass.
int main() try {
    Tables tables;
    tables.item_info["cap"] = ItemInfo{};
    int bad = 0;
    for (const auto& one : kCases) {
        auto items = worn(one);
        const auto got = stamped(items, tables);
        std::vector<int> want;
        for (const int slot : one.order) if (slot >= 0) want.push_back(slot);
        if (got != want && ++bad <= 5) std::printf("DIFF: %zu stamped, game %zu\n", got.size(), want.size());
    }
    std::printf("%s: %zu FUN_0055dbc0 cases, %d differ\n", bad ? "FAIL" : "OK", std::size(kCases), bad);
    if (bad) return 1;

    // Two pieces of set 0 (rows 0 and 1) worn in slots 1 and 3: the list's piece is slot 3's after a pass.
    tables.sets.resize(2);
    tables.sets[0].set = tables.sets[1].set = 0;
    tables.sets[0].member = 0; tables.sets[1].member = 1;
    std::vector<d2d::d2s::Item> items(2);
    for (int index = 0; index < 2; ++index) {
        auto& item = items[std::size_t(index)];
        item.code = "cap"; item.identified = true; item.quality = 5; item.set_id = index;
        item.location = d2d::d2s::item_location::kEquipped;
        item.slot = index == 0 ? 3 : 1;
    }
    stats_on(items, items[1]);                                  // slot 1 went on last
    (void)stamped(items, tables);
    assert(set_list_piece(tables, items, 0) == &items[0]);
    // Slot 1's piece breaks: the pass, then the set's list freed.
    items[1].flags |= 0x100u;
    (void)stamped(items, tables);
    free_set_list(tables, items, items[1]);
    assert(set_pieces_worn(tables, items, 0) == 1 && set_list_piece(tables, items, 0) == nullptr);
    // The next pass (any item move) builds it again with the piece still worn.
    (void)stamped(items, tables);
    assert(set_list_piece(tables, items, 0) == &items[0]);
    std::puts("OK: the last loop rebuilds each worn set's list in slot order; a break frees it till the next pass");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_set_settle: %s\n", error.what());
    return 1;
}
