// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// place_overhead (FUN_0049e070, chat.md "Drawing an overhead message"): a
// box that meets no earlier one this frame keeps its spot; one that does
// moves by the 0x722654 steps to a 400 x 0x118 spot on screen that meets
// none, else isn't drawn; from 16 placed on, none is checked.
#include "overhead.hpp"

#include <cassert>
#include <cstdio>
#include <exception>
#include <optional>
#include <vector>

using d2d::client::OverheadRect;
using d2d::client::place_overhead;

int main() try {
    std::vector<OverheadRect> placed;
    const OverheadRect first{ 100, 100, 180, 121 };
    assert(place_overhead(placed, first, 800, 600)->left == 100 && placed.size() == 1);
    // Beside it, touching only at the edge: IntersectRect is empty, it stays.
    const OverheadRect beside{ 180, 100, 260, 121 };
    const auto kept = place_overhead(placed, beside, 800, 600);
    assert(kept && kept->left == 180 && kept->top == 100 && kept->right == 260 && placed.size() == 2);
    // On top of the first: step 1 x (0x32, 0x23) down-right meets nothing
    // (both earlier boxes end at y 121): 150, 135, 400 x 0x118.
    const auto moved = place_overhead(placed, first, 800, 600);
    assert(moved && moved->left == 150 && moved->top == 135 && moved->right == 550 && moved->bottom == 415 && placed.size() == 3);
    // Against the bottom right, every down-right spot is off screen and
    // every up-left one in reach meets a slot covering the screen: dropped.
    std::vector<OverheadRect> full{ { 0, 0, 800, 600 } };
    assert(!place_overhead(full, { 700, 500, 780, 521 }, 800, 600) && full.size() == 1);
    // From the top left only down-right exists; past a slot over the
    // first spots the next y step (0x46) is tried before a wider x.
    std::vector<OverheadRect> top_left{ { 0, 0, 100, 30 }, { 60, 40, 70, 60 } };
    const auto stepped = place_overhead(top_left, { 10, 5, 90, 26 }, 800, 600);
    assert(stepped && stepped->left == 60 && stepped->top == 75 && top_left.size() == 3);
    // 16 placed: the caller skips the check, the box stays where it is.
    std::vector<OverheadRect> sixteen(16, OverheadRect{ 0, 0, 800, 600 });
    const auto unchecked = place_overhead(sixteen, first, 800, 600);
    assert(unchecked && unchecked->left == 100 && sixteen.size() == 16);
    std::puts("overhead place ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
