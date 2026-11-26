// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// test-sources: apps/d2d/input/loot_chooser.cpp apps/d2d/input/loot_radial.cpp
#include "input/loot_chooser.hpp"
#include "input/touch/input.hpp"

#include <cassert>
#include <cstdio>
#include <exception>
#include <vector>

int main() try {
    using namespace d2d::client;
    using touch::Phase;
    std::vector<LootChoice> items;
    for (int id = 0; id < 10; ++id) items.push_back({ 100 + id, "Item", { 255, 255, 255 } });
    LootChooser chooser;
    chooser.start(items, true);
    const auto box = LootChooser::list_box();
    const float x = box.x + 10.f, y = box.y + 10.f;
    assert(chooser.touch_event(1, Phase::Down, x, y) == -1);
    assert(chooser.touch_event(1, Phase::Up, x, y) == 100);
    assert(chooser.touch_event(1, Phase::Down, x, y + 100.f) == -1);
    assert(chooser.touch_event(1, Phase::Move, x, y + 4.f) == -1);
    assert(chooser.touch_event(1, Phase::Up, x, y + 4.f) == -1);
    assert(chooser.scroll == 2);
    assert(chooser.touch_event(1, Phase::Down, x, y) == -1);
    assert(chooser.touch_event(1, Phase::Cancel, x, y) == -1);
    assert(chooser.touch_event(1, Phase::Up, x, y) == -1);
    assert(chooser.touch_event(1, Phase::Down, x, y) == -1);
    assert(chooser.touch_event(2, Phase::Up, x, y) == -1);
    assert(chooser.touch_event(1, Phase::Up, x, y) == 102);
    assert(chooser.touch_event(-2, Phase::Down, x, y) == -1);
    assert(chooser.touch_event(-2, Phase::Move, x + 20.f, y) == -1);
    assert(chooser.touch_event(-2, Phase::Up, x, y) == -1); // horizontal drag stays cancelled
    assert(chooser.touch_event(1, Phase::Down, x - 20.f, y) == -1);
    assert(chooser.touch_event(1, Phase::Up, x, y) == -1); // cannot enter a row from outside
    chooser.clear();
    assert(!chooser.open && chooser.choices.empty());
    return 0;
} catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
