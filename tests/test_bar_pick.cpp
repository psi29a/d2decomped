// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// The life-bar choice (apps/d2d/bar_pick.hpp). The caller supplies only
// monsters the frame is drawing. This does not run Town.
#include "bar_pick.hpp"

#include <cassert>
#include <vector>

using d2d::client::BarFoe;
using d2d::client::bar_monster;

int main() {
    const std::vector<BarFoe> drawn{ { 1, 2.f, 0.f, true, 0 }, { 2, 4.f, 0.f, true, 40 }, { 3, 1.f, 0.f, true, 20 }, { 4, 0.5f, 0.f, false, 80 } };
    assert(bar_monster(1, 2, drawn, 0.f, 0.f) == 1);
    assert(bar_monster(-1, 2, drawn, 0.f, 0.f) == 2);
    assert(bar_monster(-1, -1, drawn, 0.f, 0.f) == 3);
    assert(bar_monster(9, -1, drawn, 0.f, 0.f) == 3);
    const std::vector<BarFoe> ally{ { 4, 0.5f, 0.f, false, 80 } };
    assert(bar_monster(-1, -1, ally, 0.f, 0.f) == -1);
    assert(d2d::client::bar_on_screen(240.f, 40.f));
    assert(!d2d::client::bar_on_screen(640.f, 0.f));
    std::vector<BarFoe> hidden{ drawn[2] };
    hidden[0].on_screen = false;
    assert(bar_monster(-1, -1, hidden, 0.f, 0.f) == -1);
    return 0;
}
