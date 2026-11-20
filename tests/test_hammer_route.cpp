// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Blessed Hammer flies FUN_0067a140's spiral (path type 14): rules::hammer_route
// against game.exe's own points (tools/emu/hammer_route.py).

#include <missiles.hpp>

#include <cassert>
#include <exception>
#include <utility>
#include <vector>

int main() try {
    struct Route { int x, y; std::vector<std::pair<int, int>> points; };
    const std::vector<Route> routes{
#include "hammer_route_cases.inc"
    };
    for (const auto& route : routes) assert(d2d::rules::hammer_route(route.x, route.y) == route.points);
    return 0;
} catch (const std::exception&) {
    return 1;
}
