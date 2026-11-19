// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: d2s mpq compcode
// A save below version 0x5d (0x5c, the oldest FUN_0056a090 takes) reads its
// properties with ItemStatCost's "1.09-Save Bits" / "1.09-Save Add" (record
// +0x1a / +0x20, FUN_0062cbe0's index 1), no param bits (+0x28 is no column),
// and keeps a few stats as their 1.10 neighbours (FUN_0062ac80: 0x5e as
// 0x5d, 0x61 as 0x60, 100 as 99, 0x67 as 0x66, 0x6a as 0x69). Defence is row
// 31's widths. Synthetic tables: no game data.
#include <d2s_items.hpp>
#include <d2s_write.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::d2s;

int main() try {
    ItemTables tables;
    tables.stats.resize(0x6c);
    tables.stats[kArmorClass] = { 11, 10, 0, 0, 10, 10 };            // 1.14d's row 31
    tables.stats[0x5e] = { 7, 0, 0, 0, 6, 20 };                       // item_addexperience's neighbour
    tables.stats[107] = { 3, 0, 9, 0, 14, 0 };                        // item_singleskill: 3 + 9 param bits in 1.14d, 14 in 1.09

    // FUN_0062ac80's remaps happen only below 0x5d.
    assert(detail::stat_109(0x5e, 0x5c) == 0x5d && detail::stat_109(0x61, 0x5c) == 0x60 && detail::stat_109(100, 0x5c) == 99);
    assert(detail::stat_109(0x67, 0x5c) == 0x66 && detail::stat_109(0x6a, 0x5c) == 0x69 && detail::stat_109(0x5c, 0x5c) == 0x5d);
    assert(detail::stat_109(0x5e, 0x5d) == 0x5e && detail::stat_109(107, 0x5c) == 107);
    const auto old_width = detail::width(tables.stats[107], 0x5c), new_width = detail::width(tables.stats[107], 0x60);
    assert(old_width.bits == 14 && old_width.add == 0 && old_width.param == 0);
    assert(new_width.bits == 3 && new_width.add == 0 && new_width.param == 9);

    // A property list as a 0x5c save writes it: 0x5e at 6 bits less 20, 107
    // at 14 bits with no param, then the end.
    detail::BitWriter old_list;
    old_list.write(0x5e, 9); old_list.write(25 + 20, 6);
    old_list.write(107, 9); old_list.write(3, 14);
    old_list.write(0x1ff, 9);
    detail::Bits old_bits{ old_list.out, 0, 0x5c };
    std::vector<ItemProp> props;
    detail::props(old_bits, tables, props);
    assert(props.size() == 2);
    assert(props[0].stat == 0x5d && props[0].value == 25);   // read with 0x5e's widths, kept as 0x5d
    assert(props[1].stat == 107 && props[1].param == 0 && props[1].value == 3);

    // The same stats in a 1.14d save: their own ids, 1.14d's widths.
    detail::BitWriter new_list;
    new_list.write(0x5e, 9); new_list.write(25, 7);
    new_list.write(107, 9); new_list.write(149, 9); new_list.write(3, 3);
    new_list.write(0x1ff, 9);
    detail::Bits new_bits{ new_list.out, 0, 0x60 };
    props.clear();
    detail::props(new_bits, tables, props);
    assert(props.size() == 2 && props[0].stat == 0x5e && props[0].value == 25);
    assert(props[1].stat == 107 && props[1].param == 149 && props[1].value == 3);

    assert(detail::width(tables.stats[kArmorClass], 0x5c).bits == 10 && detail::width(tables.stats[kArmorClass], 0x60).bits == 11);
    std::puts("OK: a 0x5c save's property widths and stat ids (FUN_0062cbe0, FUN_0062ac80)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
