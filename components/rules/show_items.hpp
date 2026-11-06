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
inline bool label_try(LabelRect& rect, const std::vector<LabelRect>& placed, int attempt, int max_x, int min_x, int screen_height) {
    const int bottom_limit = screen_height - 0x30;
    if (rect.left < min_x) { rect.right += min_x - rect.left; rect.left = 6; }
    if (rect.top < 6) { rect.bottom += 6 - rect.top; rect.top = 6; }
    if (rect.right >= max_x) { const int over = rect.right - max_x + 1; rect.right -= over; rect.left -= over; }
    if (rect.bottom >= bottom_limit) { const int over = rect.bottom - bottom_limit; rect.bottom -= over; rect.top -= over; }
    for (const auto& other : placed) {
        if (!(rect.left <= other.right && rect.right >= other.left && rect.top <= other.bottom && rect.bottom >= other.top)) continue;
        if (attempt <= 6) {
            const int delta = (rect.bottom - rect.top) / 2 != 0 && rect.top <= bottom_limit ? other.top - rect.bottom - 3 : other.bottom - rect.top + 5;
            rect.top += delta; rect.bottom += delta;
        } else {
            const int delta = (rect.right - rect.left) / 2 != 0 && rect.left >= max_x ? other.left - rect.right - 1 : other.right - rect.left + 1;
            rect.left += delta; rect.right += delta;
        }
        return false;
    }
    return true;
}

// FUN_004c0810's loop for one item whose spot is (x, y): none for a spot
// outside min_x..max_x or -8..height - 0x10; else up to 12 tries, the
// label kept (added to `placed`) when one comes clear and it then lies in
// min_x..max_x. Returns the tries made; `rect` as the last left it.
inline int label_place(LabelRect& rect, std::vector<LabelRect>& placed, int x, int y, int max_x, int min_x, int screen_height, bool& kept) {
    int tries = 0;
    bool clear = false;
    if (x >= min_x && x <= max_x && y >= -8 && y <= screen_height - 0x10)
        while (tries < 12 && !clear) clear = label_try(rect, placed, tries++, max_x, min_x, screen_height);
    kept = clear && rect.right <= max_x && rect.left >= min_x;
    if (kept) placed.push_back(rect);
    return tries;
}

}  // namespace d2d::rules
