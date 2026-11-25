// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// The order overhead texts take their slots in (FUN_004a0e70): the players'
// table, the monsters', the objects' (FUN_00464950 / 00464990 / 004649d0 over
// 0x7a5e70 / 0x7a6070 / 0x7a6270), each walked by FUN_00463c90 from bucket 0 to
// 0x7f along the +0xe4 chains. FUN_00463a50 keeps a chain highest id first.
// tools/emu/unit_table_order.py runs the game's insert and walk on the same ids.
#include "render/overhead.hpp"

#include <cassert>
#include <cstdio>
#include <exception>
#include <span>
#include <vector>

using d2d::client::overhead_order;
using d2d::client::overhead_walk_before;

int main() try {
    // A bucket is id & 0x7f; a lower bucket first, a higher id first within one.
    assert(overhead_walk_before(1, 2) && !overhead_walk_before(2, 1));
    assert(overhead_walk_before(0x81, 0x01) && !overhead_walk_before(0x01, 0x81));   // one bucket: 0x81 above 0x01
    assert(overhead_walk_before(0x80, 0x02) && !overhead_walk_before(0x02, 0x80));   // bucket 0 before bucket 2
    assert(!overhead_walk_before(5, 5));
    assert(overhead_walk_before(0xffffff81u, 0x81) && !overhead_walk_before(0x81, 0xffffff81u));   // unsigned
    struct Speaker { int id, table; bool speaks; };
    // Objects after monsters whatever the unit order; a silent unit takes no slot.
    const std::vector<Speaker> crowd{ { 9, 2, true }, { 7, 1, true }, { 4, 0, true }, { 8, 1, false }, { 6, 1, true }, { 0x81, 1, true } };
    const auto order = overhead_order(std::span<const Speaker>(crowd), [](const Speaker& unit) { return unit.speaks; },
                                      [](const Speaker& unit) { return unit.table; }, [](const Speaker& unit) { return unit.id; });
    // players 4; monsters by bucket: 0x81 (bucket 1), 6, 7; objects 9.
    assert(order.size() == 5 && order[0]->id == 4 && order[1]->id == 0x81 && order[2]->id == 6 && order[3]->id == 7 && order[4]->id == 9);
    // No ids to give: the table order holds and each table keeps the unit order.
    const auto stable = overhead_order(std::span<const Speaker>(crowd), [](const Speaker& unit) { return unit.speaks; },
                                       [](const Speaker& unit) { return unit.table; }, [](const Speaker&) { return 0; });
    assert(stable.size() == 5 && stable[0]->id == 4 && stable[1]->id == 7 && stable[2]->id == 6 && stable[3]->id == 0x81 && stable[4]->id == 9);
    std::puts("overhead walk ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
