// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: dcc font game
// test-includes: apps/d2d
// The Party screen's roster order (FUN_00479ab0, party.md "More roster
// functions"): party groups by word ascending, no party (0xffff) last, our
// own party's group moved to the front (FUN_004798c0).
#include "party.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

using d2d::client::PartyPanel;

namespace {

std::vector<std::uint32_t> ids(int own_party) {
    std::vector<PartyPanel::Row> rows{
        { .id = 5, .party = 0xffff }, { .id = 4, .party = 2 }, { .id = 3, .party = 1 },
        { .id = 2, .party = 2 },      { .id = 1, .party = 0xffff },
    };
    PartyPanel::order(rows, own_party);
    std::vector<std::uint32_t> order;
    for (const auto& row : rows) order.push_back(row.id);
    return order;
}

}  // namespace

int main() try {
    assert((ids(0xffff) == std::vector<std::uint32_t>{ 3, 2, 4, 1, 5 }));
    assert((ids(2) == std::vector<std::uint32_t>{ 2, 4, 3, 1, 5 }));
    assert((ids(1) == std::vector<std::uint32_t>{ 3, 2, 4, 1, 5 }));
    std::puts("party order ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
