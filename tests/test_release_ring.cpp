// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// FUN_005d4150 and FUN_005d4870 share one 64-point ring. The walk steps
// prgcalc entries while the byte offset stays under 0x100. FUN_004efc20(0x118)
// is that skill's Param2; both releases store it plus 1, and 143 only when
// the missile row is 0x238.

#include <skills.hpp>

#include <cassert>
#include <exception>
#include <utility>
#include <vector>

int main() try {
    using d2d::rules::kReleaseRing;
    assert(kReleaseRing.size() == 64);
    assert(kReleaseRing.front() == std::pair(30, 0));
    assert(kReleaseRing.back() == std::pair(29, -2));

    std::vector<std::pair<int, int>> all;
    d2d::rules::visit_release_ring(1, [&](std::pair<int, int> offset) { all.push_back(offset); });
    assert(all.size() == 64);
    assert(all.front() == kReleaseRing.front());

    std::vector<std::pair<int, int>> stepped;
    d2d::rules::visit_release_ring(4, [&](std::pair<int, int> offset) { stepped.push_back(offset); });
    assert(stepped.size() == 16);
    assert(stepped.front() == kReleaseRing[0]);
    assert(stepped[1] == kReleaseRing[4]);
    assert(stepped.back() == kReleaseRing[60]);

    std::size_t one = 0;
    d2d::rules::visit_release_ring(0, [&](std::pair<int, int>) { ++one; });
    assert(one == 64);

    assert(d2d::rules::release_ring_hops(5, 1, false) == 6);
    assert(d2d::rules::release_ring_hops(5, d2d::rules::kReleaseHopMissile, true) == 6);
    assert(d2d::rules::release_ring_hops(5, 1, true) == 0);
    return 0;
} catch (const std::exception&) {
    return 1;
}
