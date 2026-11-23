// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// FUN_004336c0's classic and expansion title-record selections.
#include <frontend/title_rules.hpp>

#include <array>
#include <cassert>
#include <cstdio>

int main() {
    assert((d2d::client::title_primary_bottoms(false) == std::array{ 224, 266, 291, 333 }));
    assert((d2d::client::title_primary_bottoms(true) == std::array{ 324, 366, 391, 433 }));
    std::puts("title positions: ok");
}
