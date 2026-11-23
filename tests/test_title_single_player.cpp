// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// LAB_00435cd0's no-save destination.
#include <frontend/title_rules.hpp>

#include <cassert>
#include <cstdio>

int main() {
    assert(d2d::client::title_single_player_creates(false));
    assert(!d2d::client::title_single_player_creates(true));
    std::puts("title single player: ok");
}
