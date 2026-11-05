// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A stack's price (FUN_0062efb0): a stackable that isn't a tome or quiver
// is priced one, its quality extras divided by its most (Items.txt
// maxstack plus stat 0xfe, FUN_006295b0), and multiplied by its quantity
// after the NPC multipliers. A stack its repair refills sells as a full
// one less the repair of what it lacks. No game data: a key, a javelin
// and throwing knives typed in.

#include <d2s.hpp>
#include <d2s_items.hpp>
#include <rules.hpp>

#include <cassert>

using namespace d2d::rules;
using d2d::d2s::Item;

int main() {
    Tables tables;
    auto& smith = tables.npc_prices["Charsi"];
    smith.sell = 256;
    smith.max_buy = { 100000, 100000, 100000 };
    tables.types["key"] = {};
    tables.types["jave"] = {};
    tables.item_info["key"] = { .type = "key", .kind = 3 };
    tables.item_info["jav"] = { .type = "jave", .kind = 3 };
    tables.item_base["key"] = { .cost = 25, .stackable = true, .max_stack = 12 };
    tables.item_base["jav"] = { .cost = 100, .stackable = true, .max_stack = 60 };
    tables.prefix_cost = { { 0, 0 }, { 2048, 600 } };
    const d2d::d2s::Header header;

    Item keys;
    keys.code = "key";
    keys.identified = true;
    keys.quantity = 4;
    assert(item_price(tables, keys, "Charsi", false, header) == 25 * 4);
    assert(item_price(tables, keys, "Charsi", true, header) == 25 * 256 / 1024 * 4);
    keys.quantity = 0;                                                     // no quantity counts as 1
    assert(item_price(tables, keys, "Charsi", false, header) == 25);
    // x quantity in 32 bits (0062fc75): 100,000,000 (99,999,744 past the
    // mult's / 1024) x 30 passes 2^31, goes negative, and costs 1.
    tables.item_base["key"].cost = 100000000;
    keys.quantity = 30;
    assert(item_price(tables, keys, "Charsi", false, header) == 1);
    tables.item_base["key"].cost = 25;

    // A magic javelin: its prefix's 2048 x 100 / 1024 + 600 = 800 is shared
    // by the 60 a stack holds (13 each), then the 20 here are paid for.
    Item javelins;
    javelins.code = "jav";
    javelins.identified = true;
    javelins.quality = 4;
    javelins.prefix = 1;
    javelins.quantity = 20;
    assert(stack_most(tables, javelins) == 60 && stack_divisor(tables, javelins) == 60);
    assert(item_price(tables, javelins, "Charsi", false, header) == (100 + 800 / 60) * 20);
    javelins.props = { { .stat = kExtraStack, .param = 0, .value = 40 } };  // item_extra_stack: 100 a stack
    assert(stack_most(tables, javelins) == 100);
    assert(item_price(tables, javelins, "Charsi", false, header) == (100 + 800 / 100) * 20);

    // A stack its repair refills (Repair type, FUN_0062e660 passes): it sells
    // as a full one less the repair of what it lacks (0062fcaf), and its
    // repair is a unit's after the mults times that (0062fcb6). Gheed: a
    // sale 512 / 1024 (50 a knife), a repair 256 / 1024 (25 a knife).
    auto& gheed = tables.npc_prices["Gheed"];
    gheed.sell = 512;
    gheed.rep = 256;
    gheed.max_buy = { 100000, 100000, 100000 };
    tables.types["tkni"].repair = true;
    tables.item_info["tkf"] = { .type = "tkni", .kind = 2 };
    tables.item_base["tkf"] = { .cost = 100, .stackable = true, .max_stack = 50 };
    Item knives;
    knives.code = "tkf";
    knives.identified = true;
    knives.quantity = 30;
    assert(stack_missing(tables, knives) == 20);
    assert(item_price(tables, knives, "Gheed", true, header) == 50 * 50 - 25 * 20);
    assert(repair_cost(tables, knives, "Gheed", header) == 25 * 20);
    knives.quantity = 50;                                                  // full: a full one's price, the repair 0 (then 1)
    assert(item_price(tables, knives, "Gheed", true, header) == 50 * 50 && repair_cost(tables, knives, "Gheed", header) == 1);
    knives.quantity = 30;
    knives.props = { { .stat = kReplenishQuantity, .param = 0, .value = 1 } };   // replenishes itself: nothing lacks
    assert(stack_missing(tables, knives) == 0 && item_price(tables, knives, "Gheed", true, header) == 50 * 50);
    knives.props = { { .stat = kExtraStack, .param = 0, .value = 10 } };          // a most of 60: 30 lack
    assert(item_price(tables, knives, "Gheed", true, header) == 60 * 50 - 25 * 30);
    assert(repair_cost(tables, knives, "Gheed", header) == 25 * 30);
    knives.quantity = 50;
    assert(needs_repair(tables, knives));
    assert(mend(tables, knives) && knives.quantity == 60);
    knives.identified = false;                                             // the gate refuses: sold x quantity
    assert(item_price(tables, knives, "Gheed", true, header) == 50 * 60);
    return 0;
}
