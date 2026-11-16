// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// FUN_005d5080: Royal Strike's third release aims each missile at
// seed % 0x28 - 0x14 subtiles on each axis. A (0, 0) pair is stored
// as x = 0x14. The old roll, rng(41) - 20, could land on +20 or on
// (0, 0).
// FUN_005d3f90: the scatter release (srvprgfunc 39) offsets each try by
// radius - rand(2 x radius) on x, then y (0x5d4080..0x5d409c), so
// -radius + 1..radius; the old roll, rand(2 x radius + 1), reached -radius.

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
    for (int sample_index = 0; sample_index < 64; ++sample_index) {
        const auto [offset_x, offset_y] = d2d::rules::release_spread_offset(walk);
        assert(offset_x >= -20 && offset_x <= 20);
        assert(offset_y >= -20 && offset_y <= 19);
        assert(!(offset_x == 0 && offset_y == 0));
        if (offset_x == 0x14) assert(offset_y == 0);
    }

    d2d::rules::Rng scatter{ 1 };
    assert(d2d::rules::release_scatter_offset(scatter, 3) == std::pair(0, 0));
    assert(d2d::rules::release_scatter_offset(scatter, 3) == std::pair(1, -2));
    d2d::rules::Rng scatter_walk{ 7 };
    bool reached_top = false;
    for (int sample_index = 0; sample_index < 2000; ++sample_index) {
        const auto [offset_x, offset_y] = d2d::rules::release_scatter_offset(scatter_walk, 3);
        assert(offset_x >= -2 && offset_x <= 3 && offset_y >= -2 && offset_y <= 3);
        reached_top = reached_top || offset_x == 3;
    }
    assert(reached_top);
    d2d::rules::Rng no_radius{ 5 };
    assert(d2d::rules::release_scatter_offset(no_radius, 0) == std::pair(0, 0) && no_radius.low == 5);   // rand(0): 0, the seed not stepped
    return 0;
} catch (const std::exception&) {
    return 1;
}
