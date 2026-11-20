// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The Shadow Master's scan (FUN_005eb6d0) goes over the units of its near
// rooms in their lists' order, the owner and its fellow pets among the
// monsters (Fight's master scene: room_order by UnitState::room_stamp).
// rules::shadow_scan takes the owner's index: wherever the owner and a trap
// sit in the list, the counts and the picks come out the same.
#include <shadows.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <vector>

using d2d::rules::ShadowUnit;

int main() try {
    const ShadowUnit pet{ .type = 1, .x = 50, .y = 50, .foe = false };
    const ShadowUnit owner{ .type = 0, .x = 52, .y = 50, .targetable = false, .foe = false };
    const ShadowUnit trap{ .type = 1, .x = 49, .y = 49, .targetable = false, .foe = false, .side = true, .monster_id = 0x19a };
    const ShadowUnit near_foe{ .type = 1, .x = 55, .y = 50 };
    const ShadowUnit far_foe{ .type = 1, .x = 60, .y = 58, .worth = true };
    const std::vector<ShadowUnit> first{ pet, owner, near_foe, far_foe, trap };
    const std::vector<ShadowUnit> mixed{ pet, near_foe, trap, owner, far_foe };
    const auto in_first = d2d::rules::shadow_scan(first, 0, 1);
    const auto in_mixed = d2d::rules::shadow_scan(mixed, 0, 3);
    assert(in_first.traps == 1 && in_mixed.traps == 1);
    assert(in_first.all == in_mixed.all && in_first.close_count == in_mixed.close_count && in_first.owner_close_count == in_mixed.owner_close_count);
    assert(in_first.closest == 2 && in_mixed.closest == 1);              // the same unit, near_foe
    assert(in_first.worth == 3 && in_mixed.worth == 4);                  // the same unit, far_foe
    assert(in_first.closest_distance == in_mixed.closest_distance && in_first.owner_closest_distance == in_mixed.owner_closest_distance);
    std::puts("OK: the Shadow Master's scan reads the owner wherever its room puts it");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
