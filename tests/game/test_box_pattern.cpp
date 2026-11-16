// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_0064d910's pattern 2 / 4 test (FUN_0064d7c0, 0x64d7c6..0x64d7de) is
// the fixed 3x3 box x - 1 .. x + 1, y - 1 .. y + 1, whatever the unit's
// SizeX; 1 / 3 / 5 the plus, 0 the one subtile.

#include <ds1.hpp>
#include <gamedata.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    Level open;
    open.ds1 = d2d::ds1::Map(8, 8, 1, 1);           // 40 x 40 subtiles
    open.walk.assign(40 * 40, 0);
    const auto mark = [&](int sub_x, int sub_y, std::uint16_t bits) { open.walk[std::size_t(sub_y) * 40 + std::size_t(sub_x)] = bits; };
    const float unit_x = 20.5f / 5, unit_y = 20.5f / 5;   // subtile (20, 20)
    const UnitShape box{ 2, 3, 0x40 }, wide_box{ 2, 5, 0x40 }, owned_box{ 4, 3, 0x40 }, plus{ 1, 2, 0x40 }, single{ 0, 0, 0x40 };

    assert(!open.unit_blocked(unit_x, unit_y, box));
    mark(21, 21, 0x40);                              // the box's corner, off the plus
    assert(open.unit_blocked(unit_x, unit_y, box) && open.unit_blocked(unit_x, unit_y, owned_box));
    assert(!open.unit_blocked(unit_x, unit_y, plus) && !open.unit_blocked(unit_x, unit_y, single));
    mark(21, 21, 0x01);                              // another bit: not in the mask
    assert(!open.unit_blocked(unit_x, unit_y, box));
    mark(21, 21, 0);
    mark(22, 20, 0x40);                              // two off: outside the 3x3, even for a SizeX 5 shape
    assert(!open.unit_blocked(unit_x, unit_y, box) && !open.unit_blocked(unit_x, unit_y, wide_box));
    mark(22, 20, 0);
    mark(19, 20, 0x40);                              // the plus's arm: every shape but the one subtile
    assert(open.unit_blocked(unit_x, unit_y, box) && open.unit_blocked(unit_x, unit_y, plus) && !open.unit_blocked(unit_x, unit_y, single));
    mark(19, 20, 0);
    mark(20, 20, 0x40);
    assert(open.unit_blocked(unit_x, unit_y, single) && open.unit_blocked(unit_x, unit_y, wide_box));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_box_pattern: %s\n", error.what());
    return 1;
}
