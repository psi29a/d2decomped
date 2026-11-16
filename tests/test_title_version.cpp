// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// FUN_004336c0's title version record 0x115.
#include <title_rules.hpp>

#include <cassert>
#include <cstdio>

int main() {
    const auto box = d2d::client::title_version_box();
    assert(box.x == 0 && box.bottom == 599 && box.width == 200 && box.height == 40);
    assert(box.font == 16 && box.alignment == 2 && box.text == "v 1.14d");
    std::puts("title version: ok");
}
