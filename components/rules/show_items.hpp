// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <vector>

namespace d2d::rules {

// Show Items' labels (UI\showitems.cpp, FUN_004c0810): each one's rect, in
// screen pixels, edges inclusive as game.exe compares them.
struct LabelRect { int left = 0, top = 0, right = 0, bottom = 0; };

// One try of FUN_004c0640 (0x4c0640): clamp the rect to the screen
// (a left edge under min_x goes to 6, not min_x), then move it off the
// first placed label it meets: tries 0..6 up above it (or down below it
// when under 2 high or its top is past height - 0x30), 7..11 right of it
// (left of it only when left >= max_x). True when it met none.
bool label_try(LabelRect& rect, const std::vector<LabelRect>& placed, int attempt, int max_x, int min_x, int screen_height);

// FUN_004c0810's loop for one item whose spot is (x, y): none for a spot
// outside min_x..max_x or -8..height - 0x10; else up to 12 tries, the
// label kept (added to `placed`) when one comes clear and it then lies in
// min_x..max_x. Returns the tries made; `rect` as the last left it.
int label_place(LabelRect& rect, std::vector<LabelRect>& placed, int x, int y, int max_x, int min_x, int screen_height, bool& kept);

// A ground item as FUN_004c0810 meets it: its label's spot (x, y), the
// item's client position less the view's and 16 up (FUN_00620650 -
// FUN_0045afc0, FUN_006206b0 - 0x10 - FUN_0045afd0), and its name's size
// (FUN_00502520).
struct LabelItem { int x = 0, y = 0, text_w = 0, text_h = 0; };
// The item standing at (feet_x, feet_y) on screen (where it's drawn): its
// spot 16 up, whatever its sprite's size.
constexpr LabelItem label_item(int feet_x, int feet_y, int text_w, int text_h) { return { feet_x, feet_y - 0x10, text_w, text_h }; }
// A label that's drawn: its item (an index into the walk), its rect, and
// lit (draw mode 5 in the palette's (0, 64, 128), white text) or not.
struct ShownLabel { std::size_t item = 0; LabelRect rect; bool lit = false; };

// FUN_004c0810's labels, `items` in its walk: the player's room's near list
// (FUN_00619790) in order, each room's units from its list's head (the
// newest: FUN_0064c2c0 adds at the head), the drawn items (type 4, +0xc8
// bit 7). Past 0x20 labels the rest get none. The rect: left = x - (w + 8)
// / 2 (sar), right = left + w + 8, top = y - h + 4, bottom = y; then
// label_place in the columns an open panel leaves (`open_side` 1 right, 2
// left: FUN_0045ae90). While none is lit, a label goes lit when the mouse
// is within left..right, top..bottom + 4 (inclusive), or else when its
// item is the one under the cursor (FUN_00467a10).
std::vector<ShownLabel> show_items(const std::vector<LabelItem>& items, int screen_width, int screen_height, int open_side, int mouse_x, int mouse_y,
                                   std::ptrdiff_t cursor_item);

}  // namespace d2d::rules
