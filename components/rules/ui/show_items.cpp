// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of show_items.hpp's functions; their notes are in the header.
#include "show_items.hpp"

#include <cstddef>
#include <vector>

namespace d2d::rules {

bool label_try(LabelRect& rect, const std::vector<LabelRect>& placed, int attempt, int max_x, int min_x, int screen_height) {
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

int label_place(LabelRect& rect, std::vector<LabelRect>& placed, int x, int y, int max_x, int min_x, int screen_height, bool& kept) {
    int tries = 0;
    bool clear = false;
    if (x >= min_x && x <= max_x && y >= -8 && y <= screen_height - 0x10)
        while (tries < 12 && !clear) clear = label_try(rect, placed, tries++, max_x, min_x, screen_height);
    kept = clear && rect.right <= max_x && rect.left >= min_x;
    if (kept) placed.push_back(rect);
    return tries;
}

// Past 0x20 labels the rest get none. The rect: left = x - (w + 8) / 2
// (sar), right = left + w + 8, top = y - h + 4, bottom = y; then
// label_place in the columns an open panel leaves (`open_side` 1 right, 2
// left: FUN_0045ae90). While none is lit, a label goes lit when the mouse
// is within left..right, top..bottom + 4 (inclusive), or else when its
// item is the one under the cursor (FUN_00467a10).
std::vector<ShownLabel> show_items(const std::vector<LabelItem>& items, int screen_width, int screen_height, int open_side, int mouse_x, int mouse_y,
                                   std::ptrdiff_t cursor_item) {
    constexpr std::size_t kMaxLabels = 0x20;
    const int max_x = open_side == 1 ? screen_width / 2 - 0x20 : screen_width, min_x = open_side == 2 ? screen_width / 2 : 0;
    std::vector<ShownLabel> shown;
    std::vector<LabelRect> placed;
    bool lit_one = false;
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (placed.size() >= kMaxLabels) continue;
        const auto& item = items[index];
        const int box_width = item.text_w + 8, left = item.x - (box_width >> 1);
        LabelRect rect{ left, item.y - item.text_h + 4, left + box_width, item.y };
        bool kept = false;
        (void)label_place(rect, placed, item.x, item.y, max_x, min_x, screen_height, kept);
        if (!kept) continue;
        bool lit = false;
        if (!lit_one) {
            const bool under_mouse = mouse_x >= rect.left && mouse_x <= rect.right && mouse_y >= rect.top && mouse_y <= rect.bottom + 4;
            lit = under_mouse || std::ptrdiff_t(index) == cursor_item;
            lit_one = lit;
        }
        shown.push_back({ index, rect, lit });
    }
    return shown;
}

}
