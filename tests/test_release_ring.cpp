// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// FUN_005d4150 and FUN_005d4870 share one 64-point ring. The walk steps
// prgcalc entries while the byte offset stays under 0x100. FUN_004efc20(0x118)
// is that skill's Param2; both releases store it plus 1, and 143 only when
// the missile row is 0x238. 143's missiles fly FUN_005d4680's wobbling route
// (rules::wobble_route), checked against game.exe's own (tools/emu/wobble_route.py).

#include <missiles.hpp>
#include <rules.hpp>
#include <skills.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
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

    struct Route { int x, y, to_x, to_y, range; std::uint32_t low, high; std::vector<std::pair<int, int>> points; std::uint32_t low_after, high_after; };
    const std::vector<Route> routes{
#include "wobble_route_cases.inc"
    };
    for (const auto& route : routes) {
        d2d::rules::Rng seed;
        seed.low = route.low; seed.high = route.high;
        assert(d2d::rules::wobble_route(route.x, route.y, route.to_x, route.to_y, std::min(route.range, 77), seed) == route.points);
        assert(seed.low == route.low_after && seed.high == route.high_after);
    }
    return 0;
} catch (const std::exception&) {
    return 1;
}
