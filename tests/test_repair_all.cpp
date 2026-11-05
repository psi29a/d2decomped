// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Repair All takes the worn items only. Its sum (FUN_0062fe60) walks the 13
// body slots (FUN_0063bde0(inventory, 0..0xc)), and the server
// (FUN_00578050, flag 0x80000000) repairs what that sum counted, so a worn
// axe in the inventory grid is neither priced nor repaired. The sum is paid
// at once (FUN_00576d90) or nothing is repaired, and each counted item is
// mended through FUN_005762b0 -> FUN_005761c0. No game data: an axe typed in.

#include <d2s_items.hpp>
#include <rules.hpp>

#include <cassert>
#include <vector>

using namespace d2d::rules;
using d2d::d2s::Item;

namespace {

Item worn_axe(int location, int panel) {
    Item item;
    item.code = "hax";
    item.identified = true;
    item.location = location;
    item.panel = panel;
    item.max_durability = 40;
    item.durability = 10;
    return item;
}

}  // namespace

int main() {
    Tables tables;
    tables.npc_prices["Charsi"].rep = 1024;
    tables.types["axe"].repair = true;
    tables.item_info["hax"] = { .type = "axe", .kind = 2 };
    tables.item_base["hax"] = { .cost = 1000, .durability = 40 };
    Store smith;
    smith.npc_id = "Charsi";
    constexpr int kOne = 1000 * 30 / 40;

    const Item equipped = worn_axe(d2d::d2s::item_location::kEquipped, 0);
    const Item carried = worn_axe(d2d::d2s::item_location::kStored, d2d::d2s::item_panel::kInventory);
    assert(repair_all_takes(tables, equipped) && !repair_all_takes(tables, carried));

    std::vector<Item> items{ equipped, carried };
    assert(repair_all_cost(tables, smith, items) == kOne);                 // the hover: the worn one only

    d2d::d2s::Stats wallet;
    wallet.values[d2d::d2s::kGold] = 10000;
    assert(store_repair_all(tables, smith, items, wallet) == 1);
    assert(items[0].durability == 40 && items[1].durability == 10);         // the grid's axe stays worn
    assert(wallet.get(d2d::d2s::kGold) == 10000 - kOne);
    assert(store_repair_all(tables, smith, items, wallet) == 0);            // nothing left to take: 0x9dd
    assert(wallet.get(d2d::d2s::kGold) == 10000 - kOne);

    // The sum is paid at once (FUN_00576d90), purse then stash; short of it,
    // nothing is repaired, not the first item the gold would cover (0x9e4).
    std::vector<Item> two{ equipped, equipped };
    d2d::d2s::Stats short_wallet;
    short_wallet.values[d2d::d2s::kGold] = kOne;
    short_wallet.values[d2d::d2s::kGoldBank] = kOne - 1;
    assert(!store_repair_all(tables, smith, two, short_wallet));
    assert(two[0].durability == 10 && two[1].durability == 10);
    assert(short_wallet.get(d2d::d2s::kGold) == kOne && short_wallet.get(d2d::d2s::kGoldBank) == kOne - 1);
    short_wallet.values[d2d::d2s::kGoldBank] = kOne;
    assert(store_repair_all(tables, smith, two, short_wallet) == 2);
    assert(two[0].durability == 40 && two[1].durability == 40);
    assert(short_wallet.get(d2d::d2s::kGold) == 0 && short_wallet.get(d2d::d2s::kGoldBank) == 0);

    // Unidentified with a charge missing: counted at price 0 with the worn
    // axe, but FUN_005761c0's gate (FUN_0062e660) leaves it as it is.
    Item unknown = equipped;
    unknown.identified = false;
    unknown.props = { { .stat = d2d::d2s::kChargedSkill, .param = 54 << 6 | 3, .value = 10 << 8 | 5 } };
    std::vector<Item> mixed{ unknown, equipped };
    wallet.values[d2d::d2s::kGold] = kOne;
    assert(repair_all_cost(tables, smith, mixed) == kOne);
    assert(store_repair_all(tables, smith, mixed, wallet) == 1);
    assert(mixed[0].durability == 10 && mixed[0].props[0].value == (10 << 8 | 5) && mixed[1].durability == 40);
    assert(wallet.get(d2d::d2s::kGold) == 0);
    return 0;
}
