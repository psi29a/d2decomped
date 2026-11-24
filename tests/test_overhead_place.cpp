// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// place_overhead (FUN_0049e070, chat.md "Drawing an overhead message"): a
// box that meets no earlier one this frame keeps its spot; one that does
// moves by the 0x722654 steps to a 400 x 0x118 spot on screen that meets
// none, else isn't drawn; from 16 placed on, none is checked.
#include "render/overhead.hpp"

#include <cassert>
#include <cstdio>
#include <exception>
#include <optional>
#include <span>
#include <vector>

using d2d::client::overhead_frame_slots;
using d2d::client::overhead_rects_meet;
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
    // With the NPC speech up the frame starts with its box (FUN_004a0e70): 0x145 x 0x70 at ((W - 0x145) / 2, 12),
    // so an overhead text there moves off it; with none the frame starts empty.
    assert(overhead_frame_slots(std::nullopt).empty());
    auto speech = overhead_frame_slots(d2d::client::speech_slot(800));
    assert(speech.size() == 1 && speech[0].left == 237 && speech[0].top == 12 && speech[0].right == 562 && speech[0].bottom == 124);
    const auto off_box = place_overhead(speech, { 300, 50, 380, 71 }, 800, 600);
    assert(off_box && !overhead_rects_meet(*off_box, speech[0]));
    // The players' table before the monsters' (FUN_004a0e70), each in its own order; a silent unit takes no slot.
    struct Speaker { int id; bool player, speaks; };
    const std::vector<Speaker> crowd{ { 1, false, true }, { 2, true, true }, { 3, false, false }, { 4, false, true }, { 5, true, true } };
    const auto order = d2d::client::overhead_order(std::span<const Speaker>(crowd), [](const Speaker& unit) { return unit.speaks; }, [](const Speaker& unit) { return unit.player ? 0 : 1; },
                                                [](const Speaker&) { return 0; });
    assert(order.size() == 4 && order[0]->id == 2 && order[1]->id == 5 && order[2]->id == 1 && order[3]->id == 4);
    std::puts("overhead place ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
