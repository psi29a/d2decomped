// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A whole item's repair price is 1, and Repair All doesn't count it.
// FUN_0062efb0 mode 3 prices an identified, non-ethereal item its gate
// (FUN_0062e660) lets through, worn or not: a whole one comes to 0 and
// takes the minimum, 1 (0062fd5c). Repair All (FUN_0062fe60) skips it by
// its own test: durability != max, a restocking stack short of its max, or
// a charge missing. No game data: an axe and a throwing knife typed in.

#include <d2s.hpp>
#include <d2s_items.hpp>
#include <rules.hpp>

#include <cassert>
#include <vector>

using namespace d2d::rules;
using d2d::d2s::Item;

namespace {

Item carried(const char* code) {
    Item item;
    item.code = code;
    item.identified = true;
    item.location = d2d::d2s::item_location::kStored;
    item.panel = d2d::d2s::item_panel::kInventory;
    return item;
}

}  // namespace

int main() {
    Tables tables;
    tables.npc_prices["Charsi"].rep = 1024;
    tables.types["axe"].repair = true;
    tables.types["tkni"].repair = true;
    tables.item_info["hax"] = { .type = "axe", .kind = 2 };
    tables.item_info["tkf"] = { .type = "tkni", .kind = 2 };
    tables.item_base["hax"] = { .cost = 1000, .durability = 40 };
    tables.item_base["tkf"] = { .cost = 600, .stackable = true, .max_stack = 60 };
    const d2d::d2s::Header header;
    Store smith;
    smith.npc_id = "Charsi";

    Item whole = carried("hax");
    whole.max_durability = whole.durability = 40;
    assert(repair_cost(tables, whole, "Charsi", header) == 1);            // the gate lets it through; 0, then the minimum
    assert(!needs_repair(tables, whole));

    Item worn = whole;
    worn.durability = 10;
    assert(needs_repair(tables, worn) && repair_cost(tables, worn, "Charsi", header) == 1000 * 30 / 40);

    Item over = whole;                                                     // FUN_0062fe60 asks durability != max
    over.durability = 41;
    assert(needs_repair(tables, over));

    // Repair All's sum, both on the body, counts the worn axe only: not 1 for the whole one.
    std::vector<Item> items{ whole, worn };
    for (auto& item : items) item.location = d2d::d2s::item_location::kEquipped;
    assert(repair_all_cost(tables, smith, items) == 1000 * 30 / 40);

    // A single repair of a whole item takes nothing and changes nothing.
    d2d::d2s::Stats wallet;
    wallet.values[d2d::d2s::kGold] = 100;
    assert(!store_repair(tables, smith, whole, wallet) && wallet.get(d2d::d2s::kGold) == 100);

    // A full restocking stack: 1 too, and no repair; one short of its max needs one.
    Item knives = carried("tkf");
    knives.quantity = 60;
    assert(repair_cost(tables, knives, "Charsi", header) == 1 && !needs_repair(tables, knives));
    knives.quantity = 59;
    assert(needs_repair(tables, knives));

    // Unidentified: the gate refuses it (0), and it needs no repair; with a
    // charge missing Repair All counts it, at that price 0.
    Item unknown = worn;
    unknown.identified = false;
    assert(repair_cost(tables, unknown, "Charsi", header) == 0 && !needs_repair(tables, unknown));
    unknown.props = { { .stat = d2d::d2s::kChargedSkill, .param = 54 << 6 | 3, .value = 10 << 8 | 5 } };
    assert(needs_repair(tables, unknown) && repair_cost(tables, unknown, "Charsi", header) == 0);
    assert(!store_repair(tables, smith, unknown, wallet) && wallet.get(d2d::d2s::kGold) == 100);
    return 0;
}
