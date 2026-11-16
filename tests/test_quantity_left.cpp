// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Whether ammunition has some left (FUN_0062a310): rules::quantity_left.
// game.exe's own is run by tools/emu/items_traces2.py: a quantity above 0;
// else stat 0x7d (item_throwable); else no max stack. No game data needed.
#include <ammo.hpp>
#include <d2s_items.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::rules;

int main() try {
    assert(quantity_left(1, false, 80));           // some left
    assert(!quantity_left(0, false, 80));          // none, and it stacks
    assert(!quantity_left(-1, false, 1));
    assert(quantity_left(0, true, 80));            // stat 0x7d: always
    assert(quantity_left(0, false, 0));            // no max stack: always
    assert(quantity_left(0, false, -5));

    Tables tables;
    tables.item_info["jav"] = { .type = "jave", .kind = 2 };
    tables.item_base["jav"] = { .stackable = true, .max_stack = 60 };
    tables.item_info["hax"] = { .type = "axe", .kind = 2 };
    tables.item_base["hax"] = {};
    d2d::d2s::Item javelins;
    javelins.code = "jav";
    javelins.quantity = 0;
    assert(!quantity_left(tables, javelins));
    javelins.quantity = 3;
    assert(quantity_left(tables, javelins));
    javelins.quantity = 0;
    javelins.props = { { .stat = kItemThrowable, .param = 0, .value = 1 } };
    assert(quantity_left(tables, javelins));
    d2d::d2s::Item axe;
    axe.code = "hax";
    assert(quantity_left(tables, axe));            // no stack to run out
    std::printf("OK: quantity left as FUN_0062a310\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_quantity_left: %s\n", error.what());
    return 1;
}
