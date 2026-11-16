// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: dcc font game
// test-includes: apps/d2d
// The Party screen's roster order (FUN_00479ab0, party.md "More roster
// functions"): party groups by word ascending, no party (0xffff) last, our
// own party's group moved to the front (FUN_004798c0); inside a group by
// name, a byte-wise strcmp (FUN_004795f0): "Eve" before "amy". With the
// game flags' bit 1, by the sort key (S->C 0x65's word), most first (FUN_004796d0, signed).
#include "party.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

using d2d::client::PartyPanel;

namespace {

std::vector<std::uint32_t> ids(int own_party, bool by_sort_key = false) {
    std::vector<PartyPanel::Row> rows{
        { .id = 5, .name = "Eve", .party = 0xffff, .sort_key = -2 }, { .id = 4, .name = "Dan", .party = 2, .sort_key = 1 },
        { .id = 3, .name = "Cid", .party = 1 },                   { .id = 2, .name = "Zed", .party = 2, .sort_key = 7 },
        { .id = 1, .name = "amy", .party = 0xffff },               { .id = 6, .name = "Bob", .party = 0xffff },
    };
    PartyPanel::order(rows, own_party, by_sort_key);
    std::vector<std::uint32_t> order;
    for (const auto& row : rows) order.push_back(row.id);
    return order;
}

}  // namespace

int main() try {
    assert((ids(0xffff) == std::vector<std::uint32_t>{ 3, 4, 2, 6, 5, 1 }));
    assert((ids(2) == std::vector<std::uint32_t>{ 4, 2, 3, 6, 5, 1 }));
    assert((ids(1) == std::vector<std::uint32_t>{ 3, 4, 2, 6, 5, 1 }));
    assert((ids(0xffff, true) == std::vector<std::uint32_t>{ 3, 2, 4, 1, 6, 5 }));   // 7 before 1; 0, 0 in their order, then -2
    std::puts("party order ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
