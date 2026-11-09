// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace d2d::rules {

// The hire window's rows (FUN_004b5c60; docs/research/re/npc-menu.md
// "Hire"): two lines an offer. The first: the merc's name, " - " (0xf9b,
// 0xf9c, 0xf9b), then Lvl (0xd28), Life (0xd26), Def (0xd27) and Cost
// (0xd29), each its label (at most 20 characters, FUN_00526790), ':' and
// ' ' (0xf9d, 0xf9b) and its value (hire_number), two spaces between. The
// second: two spaces and the hireling's description (hire_description).

// A row's number: "%2u" (wsprintfA), widened at most 9 characters
// (FUN_00526f20: a longer one is cut to 8).
inline std::string hire_number(std::uint32_t value) {
    std::array<char, 16> text{};
    std::snprintf(text.data(), text.size(), "%2u", value);
    std::string number(text.data());
    if (number.size() > 9) number.resize(8);
    return number;
}

// A row's label: its first 20 characters (FUN_00526790(.., 0x14)).
inline std::string hire_label(std::string_view label) { return std::string(label.substr(0, 20)); }

// The description's string (FUN_00663720: table 0x6ee410) by the hireling's
// HireDesc, its hiredesc.txt row (0 None .. 8 Lightning Spells; hireling.txt
// row +0xd2, FUN_00655720's link): 0 none.
inline constexpr std::array<std::uint16_t, 9> kHireDescStrings{ 0, 0xd2b, 0xd2c, 0x2b52, 0x2b50, 0x2b51, 0xd31, 0xd30, 0xd2f };
constexpr std::uint16_t hire_desc_string(int hire_desc) {
    return hire_desc >= 0 && hire_desc < int(kHireDescStrings.size()) ? kHireDescStrings[std::size_t(hire_desc)] : 0;
}
// The description as the row shows it: cut at its first '[' (FUN_0048be80
// with "a0:%0", FUN_0048be00: a grammar code's start).
inline std::string hire_description(std::string_view text) { return std::string(text.substr(0, text.find('['))); }

// The list box (D2Win's ListBoxImplementation, FUN_004bf650 from
// FUN_004bf8f0): the hire window's at ((W - 490) / 2, H / 2 - 160), 490 x
// 280 (FUN_004b5c60), each row's lines 14 high (+0x180766), 6 between rows
// (+0x18076a), the scroll bar at x + width - 0x12 (+0x18075a). `lines`: each
// row's lines that have text, from row 0; `first` the first shown (+0x180730).
struct ListBox { int x = 0, y = 0, width = 0, height = 0, row_gap = 6, line_height = 0xe; };
constexpr int list_scroll_x(const ListBox& box) { return box.x + box.width - 0x12; }
constexpr int hire_list_x(int screen_width) { return (screen_width - 0x1ea) / 2; }
constexpr ListBox hire_list_box(int screen_width, int screen_height) { return { hire_list_x(screen_width), screen_height / 2 - 0xa0, 0x1ea, 0x118 }; }
// The hire window (FUN_004b7eb0 with a size, no layout: +0x58 0): 490 x 350
// at ((W - 490) / 2, H / 2 - 0xc3); its header's baseline 0x15 down, then
// "cancel" 0x13b below that (FUN_004b85f0's line heights).
constexpr int hire_window_y(int screen_height) { return screen_height / 2 - 0xc3; }

// The rows that fit (FUN_004be340): from `first`, each row's lines x 14 +
// 6 summed; at the first row that takes the sum past height - 0x14, the
// rows before it (at least 1); all fitting: the whole count.
template <typename Lines> constexpr int list_rows_shown(const ListBox& box, const Lines& lines, int first) {
    int total = 0;
    for (int row = 0; row < int(lines.size()); ++row) {
        if (row >= first) total += int(lines[std::size_t(row)]) * box.line_height + box.row_gap;
        if (total > box.height - 0x14) return row - first > 0 ? row - first : 1;
    }
    return int(lines.size());
}

// A mouse move inside the box (FUN_004beb70: edges out) asks list_row_at
// for the row under it; outside, the last answer stands.
constexpr bool list_contains(const ListBox& box, int mouse_x, int mouse_y) {
    return mouse_x > box.x && mouse_x < box.x + box.width && mouse_y > box.y && mouse_y < box.y + box.height;
}
// The row under the mouse (FUN_004bea50), -1 none: not past the scroll bar
// - 0xf; from `first`, a row's lines span y + 0xc + its top .. + lines x 14
// (edges out), the gaps no row's. It looks past the rows that fit: the
// box's bottom can name one not drawn.
template <typename Lines> constexpr int list_row_at(const ListBox& box, const Lines& lines, int first, int mouse_x, int mouse_y) {
    if (mouse_x > list_scroll_x(box) - 0xf) return -1;
    int top = 0;
    for (int row = first; row < int(lines.size()); ++row) {
        const int bottom = top + int(lines[std::size_t(row)]) * box.line_height;
        if (mouse_y > box.y + 0xc + top && mouse_y < box.y + 0xc + bottom) return row;
        top = bottom + box.row_gap;
    }
    return -1;
}

// A shown line's baseline (FUN_004bee90): y + 0x19 + 6 per row shown
// before it + 14 per line drawn before it; its x is box.x + 0x20.
constexpr int list_line_y(const ListBox& box, int rows_before, int lines_before) { return box.y + 0x19 + box.row_gap * rows_before + box.line_height * lines_before; }

}  // namespace d2d::rules
