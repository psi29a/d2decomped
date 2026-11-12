// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// FUN_005d5080: Royal Strike's third release aims each missile at
// seed % 0x28 - 0x14 subtiles on each axis. A (0, 0) pair is stored
// as x = 0x14. The old roll, rng(41) - 20, could land on +20 or on
// (0, 0).

#include <missiles.hpp>
#include <rules.hpp>

#include <cassert>
#include <exception>
#include <utility>

int main() try {
    d2d::rules::Rng seed{ 1 };
    assert(d2d::rules::release_spread_offset(seed) == std::pair(11, -9));
    assert(d2d::rules::release_spread_offset(seed) == std::pair(-8, 13));

    d2d::rules::Rng one_axis{ 410 };
    assert(d2d::rules::release_spread_offset(one_axis) == std::pair(0, 19));
    d2d::rules::Rng other_axis{ 2 };
    assert(d2d::rules::release_spread_offset(other_axis) == std::pair(16, 0));

    d2d::rules::Rng both{ 4354 };
    assert(d2d::rules::release_spread_offset(both) == std::pair(0x14, 0));

    d2d::rules::Rng walk{ 7 };
    for (int k = 0; k < 64; ++k) {
        const auto [offset_x, offset_y] = d2d::rules::release_spread_offset(walk);
        assert(offset_x >= -20 && offset_x <= 20);
        assert(offset_y >= -20 && offset_y <= 19);
        assert(!(offset_x == 0 && offset_y == 0));
        if (offset_x == 0x14) assert(offset_y == 0);
    }
    return 0;
} catch (const std::exception&) {
    return 1;
}
