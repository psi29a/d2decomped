// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// An item that replenishes its durability (item_replenish_durability, stat
// 0xfc) repairs at (max - 1) / max of its price while it is below max - 1
// (FUN_0062efb0 0062fa13 / 0062fa35), else nothing (0062fa2c), however worn.
// No game data: an axe typed in.

#include <d2s.hpp>
#include <d2s_items.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::rules;
using d2d::d2s::Item;

int main() try {
    Tables tables;
    tables.npc_prices["Charsi"].rep = 1024;
    tables.types["axe"].repair = true;
    tables.item_info["hax"] = { .type = "axe", .kind = 2 };
    tables.item_base["hax"] = { .cost = 1000, .durability = 40 };
    const d2d::d2s::Header header;

    Item axe;
    axe.code = "hax";
    axe.identified = true;
    axe.max_durability = 40;
    axe.durability = 10;
    assert(repair_cost(tables, axe, "Charsi", header) == 1000 * 30 / 40);       // the plain missing share
    axe.props = { { .stat = kReplenishDurability, .param = 0, .value = 1 } };
    assert(repair_cost(tables, axe, "Charsi", header) == 1000 * 39 / 40);       // replenishing: (max - 1) / max
    axe.durability = 38;
    assert(repair_cost(tables, axe, "Charsi", header) == 1000 * 39 / 40);
    axe.durability = 39;                                                        // max - 1: nothing, then the minimum
    assert(repair_cost(tables, axe, "Charsi", header) == 1);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_repair_replenish: %s\n", error.what());
    return 1;
}
