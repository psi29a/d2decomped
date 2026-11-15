// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_00545340 (Level::free_area) hands its mask to FUN_0064d800 whole, so
// the unit bits a unit stamps in the room grid (0x1000 / 0x2000,
// FUN_0064ea90) keep a spot from being free when the mask has them. Here
// the grid is open and `units` gives those bits; mask 0x100 ignores them.

#include <ds1.hpp>
#include <gamedata.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <functional>
#include <utility>

using namespace d2d::game;

int main() try {
    constexpr int kWidth = 16, kAt = 40;
    Level level{};
    level.ds1 = d2d::ds1::Map(kWidth, kWidth, 1, 1);
    level.walk.assign(std::size_t(kWidth * 5) * std::size_t(kWidth * 5), 0);
    // Ring 1's one try: the 7 x 7 centred a subtile up-left of (x - 1, y + 1).
    assert(level.free_area(kAt, kAt, 3, 0x3f11, 100) == std::pair(kAt - 1, kAt + 1));
    // A unit on (x, y) stamping 0x1000: rings 1 and 2 all cover it; ring 3's
    // first try (row 3, column -3) doesn't.
    const std::function<int(int, int)> unit_on_spot = [](int sub_x, int sub_y) { return sub_x == kAt && sub_y == kAt ? 0x1000 : 0; };
    assert(level.free_area(kAt, kAt, 3, 0x3f11, 100, unit_on_spot) == std::pair(kAt - 3, kAt + 3));
    // Mask 0x100 (Larzuk's, Jerhyn's spots) has no unit bit: the unit is no matter.
    assert(level.free_area(kAt, kAt, 2, 0x100, 100, unit_on_spot) == level.free_area(kAt, kAt, 2, 0x100, 100));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_free_area_units: %s\n", error.what());
    return 1;
}
