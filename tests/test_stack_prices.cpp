// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A stack's price (FUN_0062efb0): a stackable that isn't a tome or quiver
// is priced one, its quality extras divided by its most (Items.txt
// maxstack plus stat 0xfe, FUN_006295b0), and multiplied by its quantity
// after the NPC multipliers. No game data: a key and a javelin typed in.

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
    return 0;
}
