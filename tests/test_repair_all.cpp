// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Repair All takes the worn items only. Its sum (FUN_0062fe60) walks the 13
// body slots (FUN_0063bde0(inventory, 0..0xc)), and the server
// (FUN_00578050, flag 0x80000000) repairs what that sum counted, so a worn
// axe in the inventory grid is neither priced nor repaired. No game data:
// an axe typed in.

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
    return 0;
}
