// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A broken item's mend (FUN_0055f900) sets item flag 0x200 as it clears 0x100:
// by a smith's repair (FUN_005761c0 -> rules::mend) and by a replenished
// point (FUN_00562c40 -> rules::replenish_fire). An unbroken item's repair
// leaves it clear (FUN_005761c0 calls FUN_0055f900 only on flag 0x100).
// No game data: an axe typed in.

#include <d2s_items.hpp>
#include <replenish.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::rules;
using d2d::d2s::Item;

int main() try {
    Tables tables;
    tables.types["axe"] = {};
    tables.types["axe"].repair = true;
    tables.item_info["hax"] = { .type = "axe", .kind = 2 };
    tables.item_base["hax"] = { .durability = 40 };

    const auto axe = [] {
        Item made;
        made.code = "hax";
        made.identified = true;
        made.durability = 10; made.max_durability = 40;
        return made;
    };

    // A smith's repair of a broken axe: 0x200 on, 0x100 off.
    Item broken_axe = axe();
    broken_axe.durability = 0; broken_axe.flags |= 0x100u;
    assert(mend(tables, broken_axe) && !broken(broken_axe) && broken_axe.durability == 40);
    assert((broken_axe.flags & kItemRepaired) == 0x200u);

    // An axe that was only worn down: durability back, the flag stays clear.
    Item worn = axe();
    assert(mend(tables, worn) && worn.durability == 40 && (worn.flags & kItemRepaired) == 0);

    // A replenished point that mends a broken stack: the same.
    tables.item_info["tkf"] = { .type = "axe", .kind = 2 };
    tables.item_base["tkf"] = { .stackable = true, .durability = 4, .max_stack = 160 };
    Item knives;
    knives.code = "tkf";
    knives.quantity = 0; knives.durability = 0; knives.max_durability = 4;
    knives.flags |= 0x100u;
    knives.props = { { .stat = kReplenishQuantity, .param = 0, .value = 25 } };
    replenish_arm(tables, knives, kReplenishQuantity);
    assert(replenish_fire(tables, knives, true) && !broken(knives) && (knives.flags & kItemRepaired) == 0x200u);

    // Not owned: no mend, no flag.
    knives.quantity = 0; knives.durability = 0; knives.flags = 0x100u;
    assert(!replenish_fire(tables, knives, false) && knives.flags == 0x100u);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_item_repaired: %s\n", error.what());
    return 1;
}
