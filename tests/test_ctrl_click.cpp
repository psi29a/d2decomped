// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// Ctrl + left click on a grid item (apps/d2d/ctrl_click.hpp, FUN_0048ffe0
// at 0x4901f4): with a store open, sold (C->S 0x33, 0x490261) if a vendor
// buys it, else the hero's "impossible" (FUN_004cb9c0 0x13, 0x490283);
// with none, nothing (0x4902f8).
#include "ctrl_click.hpp"

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::client;

int main() try {
    using ctrl_click::Outcome;
    assert(ctrl_click::outcome(true, true) == Outcome::sell);
    assert(ctrl_click::outcome(true, false) == Outcome::refused);
    // No store: the click is taken, nothing happens, sellable or not.
    assert(ctrl_click::outcome(false, true) == Outcome::none && ctrl_click::outcome(false, false) == Outcome::none);

    // The class's line, by d2s class; out of range clamps.
    assert(ctrl_click::impossible_voice(0) == "amazon_impossible_1");
    assert(ctrl_click::impossible_voice(4) == "barbarian_impossible_1");
    assert(ctrl_click::impossible_voice(6) == "assassin_impossible_1");
    assert(ctrl_click::impossible_voice(9) == "assassin_impossible_1" && ctrl_click::impossible_voice(-1) == "amazon_impossible_1");
    std::puts("ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
