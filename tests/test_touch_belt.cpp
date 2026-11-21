// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
#include "touch_belt.hpp"

#include <cassert>
#include <cstddef>

int main() {
    using d2d::client::touch::BeltStock;
    BeltStock stock;
    for (std::size_t column = 0; column < 4; ++column) assert(stock.count(column) == 0);
    // Uneven stock across rows: native boxes 0,4,8,12 share a column.
    stock.add(0);
    stock.add(4);
    stock.add(8);
    stock.add(12);
    stock.add(1);
    stock.add(9);
    stock.add(15); // A deeper item counts even if its column's top box is empty.
    assert(stock.count(0) == 4);
    assert(stock.count(1) == 2);
    assert(stock.count(2) == 0);
    assert(stock.count(3) == 1);
    stock.add(0); // Duplicate or malformed entries must not inflate the badge.
    stock.add(15);
    stock.add(-1);
    stock.add(16);
    stock.add(255);
    assert(stock.count(0) == 4 && stock.count(3) == 1);
    assert(stock.count(4) == 0);
    // A new frame starts from empty stock after consumption or character changes.
    const BeltStock empty;
    assert(empty.count(0) == 0);
    BeltStock full;
    for (int box = 15; box >= 0; --box) full.add(box);
    for (std::size_t column = 0; column < 4; ++column) assert(full.count(column) == 4);
}
