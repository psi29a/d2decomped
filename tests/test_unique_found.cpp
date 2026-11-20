// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The one-per-game uniques (game +0x1b24) over hand-made tables:
// FUN_005566b0 refuses a unique already found, but for a quest item;
// FUN_00556530 marks the one picked unless nolimit, a quest item's too.
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <vector>

using namespace d2d::rules;

int main() try {
    Tables tables;
    for (const char* code : { "hax", "nol", "hst" }) { tables.item_info[code] = { .kind = 2 }; tables.item_base[code] = { .level = 1, .durability = 20 }; }
    tables.item_base["hst"].quest = true;
    tables.uniques = { { .code = "hax" }, { .code = "nol", .nolimit = true }, { .code = "hst" } };
    std::vector<bool> found;
    auto make = [&](const char* code) {
        Rng rng(5), unit_seed(6);
        return generate_item(tables, code, 10, 7, rng, &unit_seed, &found);
    };
    // A plain unique: marked, then refused (remade as another quality).
    auto first = make("hax");
    assert(first.quality == 7 && first.unique_id == 0 && found.size() >= 1 && found[0]);
    const auto refused = make("hax");
    assert(refused.quality != 7 && refused.unique_id < 0);
    // nolimit: never marked, so it comes again.
    assert(make("nol").unique_id == 1 && !found[1] && make("nol").unique_id == 1);
    // A quest item: marked (FUN_00556530), yet never refused (FUN_005566b0 skips the test).
    assert(make("hst").unique_id == 2 && found.size() >= 3 && found[2]);
    assert(make("hst").unique_id == 2);
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
