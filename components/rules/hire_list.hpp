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

// The scroll bar (FUN_004bee90, UI\menu\textslid; each cel by its
// bottom-left): the up arrow (frame 9, 11 pressed) at (scroll x, y + 0x10),
// the track's cells (13) 12 apart below it, the down arrow (8, 10 pressed)
// on the last of them. Cells: 1 under 0x20 high, else (height - 0x10) /
// 12 - 1 (+0x180762).
constexpr int list_scroll_y(const ListBox& box) { return box.y + 0x10; }
constexpr int list_track_cells(const ListBox& box) { return box.height < 0x20 ? 1 : (box.height - 0x10) / 0xc - 1; }

// The rows wholly shown (FUN_004be110: +0x18074e): from `first`, each row
// whose running sum (lines x 14 + 6) is still under height - 0x14.
template <typename Lines> constexpr int list_rows_whole(const ListBox& box, const Lines& lines, int first) {
    int whole = 0, total = 0;
    for (int row = first; row < int(lines.size()); ++row) {
        total += int(lines[std::size_t(row)]) * box.line_height + box.row_gap;
        if (total < box.height - 0x14) ++whole;
    }
    return whole;
}

// The thumb (FUN_004be110), in cells of `length` (+0x180718: 0x12), from
// `top` to `bottom` (+0x180752 / +0x180756): none (both 0, not shown)
// while every row is whole; one from 0 to 0 still draws its ends. Its
// share is whole x 128 / rows; at the top it starts at 0, in the middle at
// length x first / rows, at the end it ends at length.
// Drawn at scroll x - 1: frame 16 on y + 0x1d + 12 x top, 14 on each cell
// to bottom, 15 one past (rules::list_thumb_cels).
struct ListThumb { int top = 0, bottom = 0; bool shown = false; };
template <typename Lines> constexpr ListThumb list_thumb(const ListBox& box, const Lines& lines, int first, int length = 0x12) {
    const int rows = int(lines.size()), whole = list_rows_whole(box, lines, first);
    if (whole >= rows) return {};
    const int share = (whole << 7) / rows;
    const int span = int(unsigned(length * share) >> 7);
    if (first == 0) return { 0, span, true };
    if (first < rows - whole) {
        const int top = length * first / rows > 0 ? length * first / rows : 0;
        return { top, span + top <= length ? span + top : length, true };
    }
    const int top = length - span;
    return { top < length ? top : length, length, true };
}
// The thumb's cels as FUN_004be110 draws them: {frame, bottom} at scroll x - 1.
struct ListCel { int frame = 0, bottom = 0; };
template <typename Out> constexpr void list_thumb_cels(const ListBox& box, ListThumb thumb, Out&& out) {
    const int top_y = list_scroll_y(box) + 0xd;
    out(ListCel{ 0x10, top_y + 12 * thumb.top });
    int last = thumb.top;
    for (int cell = thumb.top; cell < thumb.bottom; ++cell) out(ListCel{ 0xe, top_y + 12 * (cell + 1) });
    if (thumb.top < thumb.bottom) last = thumb.bottom;
    out(ListCel{ 0xf, top_y + 12 * last + 12 });
}

// The scroll bar's parts under the mouse (edges out): the up arrow
// (FUN_004be720: scroll x .. + 0x10, scroll y - 0xc .. scroll y), the down
// arrow (FUN_004be770: the same 12 above scroll y + 12 x cells), the track
// between (FUN_004be7c0: scroll y .. scroll y - 12 + 12 x cells).
enum class ListPart { kNone, kUp, kDown, kTrack };
constexpr ListPart list_part_at(const ListBox& box, int mouse_x, int mouse_y) {
    const int left = list_scroll_x(box), top = list_scroll_y(box), down = top + list_track_cells(box) * 0xc;
    if (mouse_x <= left || mouse_x >= left + 0x10) return ListPart::kNone;
    if (mouse_y > top - 0xc && mouse_y < top) return ListPart::kUp;
    if (mouse_y > down - 0xc && mouse_y < down) return ListPart::kDown;
    if (mouse_y > top && mouse_y < down - 0xc) return ListPart::kTrack;
    return ListPart::kNone;
}

// A step (FUN_004be910 up, FUN_004be990 down; a box with a callback, as the
// hire window's): the selected row (+0x180738, the one under the mouse)
// moves one, within 0 .. rows - 1, and the first shown follows it: back to
// it, or on one when it passes the rows wholly shown.
struct ListScroll { int first = 0, selected = 0; };
constexpr void list_follow(ListScroll& scroll, int whole) {
    if (scroll.selected < scroll.first) scroll.first = scroll.selected;
    if (whole + scroll.first <= scroll.selected) ++scroll.first;
}
constexpr void list_step(ListScroll& scroll, int whole, int rows, bool down) {
    if (down ? scroll.selected >= rows - 1 : scroll.selected <= 0) return;
    scroll.selected += down ? 1 : -1;
    list_follow(scroll, whole);
}
// The wheel (FUN_004be860): a notch up selects the first shown row and steps
// up; down, the last wholly shown, and steps down.
constexpr void list_wheel(ListScroll& scroll, int whole, int rows, bool down) {
    scroll.selected = down ? whole - 1 + scroll.first : scroll.first;
    list_step(scroll, whole, rows, down);
}
// A release on the track (FUN_004bf2f0), below scroll y: above the thumb
// the selection goes back the rows wholly shown (to 0 at least), below it on
// as many (to rows - 1, then to rows - whole when fewer are left); that row
// shows first. Nothing on the thumb, nor with no thumb. False: nothing.
constexpr bool list_page(ListScroll& scroll, int whole, int rows, ListThumb thumb, int scroll_y, int mouse_y) {
    if (rows == 0 || whole > rows || mouse_y <= scroll_y || thumb.top == thumb.bottom) return false;
    if (mouse_y < scroll_y + 0xd + thumb.top * 0xc) {
        scroll.selected = scroll.selected - whole > 0 ? scroll.selected - whole : 0;
    } else {
        if (mouse_y <= scroll_y + 0xd + thumb.bottom * 0xc) return false;
        scroll.selected = scroll.selected + whole < rows ? scroll.selected + whole : rows - 1;
        if (rows - scroll.selected < whole) scroll.selected = rows < whole ? 0 : rows - whole;
    }
    if (scroll.selected > 0x7f) scroll.selected = rows - 1 > 0 ? rows - 1 : 0;
    scroll.first = scroll.selected;
    list_follow(scroll, whole);
    return true;
}
// Held on an arrow (FUN_004bee90, each draw while it's pressed and under the
// mouse): the count since the press (+0x18076e) goes on one, and a step
// comes unless it's a multiple of 10; the release on it steps once more.
constexpr bool list_held_step(int& count) { ++count; return count != 0 && count % 10 != 0; }
// A key in a box with a callback (FUN_004bebc0, its key handler): the box
// takes it whatever it is (the event's +0x18, FUN_00420290); drawn twice
// since it was made (+0x180772, FUN_004bee90's count), Enter (0xd) runs
// the callback for the selected row unless that's -1. That row, or -1.
constexpr int list_enter(std::uint32_t draws, int selected, std::uint32_t key) {
    return draws >= 2 && selected != -1 && key == 0xd ? selected : -1;
}

}  // namespace d2d::rules
