// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Properties func 18, by time (FUN_0065f870 at 0x65f870 through the func
// table at 0x7462f8): the stat at param 0 with the period (param 0..3) plus
// ((max + 256) << 10 | (min + 256)) << 2, each part 0..1023; no draw.
#include <d2s_items.hpp>
#include <item_gen.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <vector>

int main() try {
    using namespace d2d::rules;
    Tables tables;
    tables.properties["ac/time"] = { { .func = 18, .stat = 268 } };
    std::vector<d2d::d2s::ItemProp> got;
    Rng still{ 9 };
    apply_mod(tables, { "ac/time", "2", 10, 50 }, got, still);
    assert(got.size() == 1 && got[0].stat == 268 && got[0].param == 0);
    assert(got[0].value == 2 + ((50 + 256) * 1024 + (10 + 256)) * 4);
    assert(still.low == 9);                                       // nothing drawn
    got.clear();
    apply_mod(tables, { "ac/time", "7", -300, 900 }, got, still);  // period to 3, min to 0, max to 1023
    assert(got.size() == 1 && got[0].value == 3 + 1023 * 1024 * 4);
    got.clear();
    apply_mod(tables, { "ac/time", "-1", 0, 0 }, got, still);      // period to 0
    assert(got.size() == 1 && got[0].value == (256 * 1024 + 256) * 4);
    std::printf("mod by time: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
