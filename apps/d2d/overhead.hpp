// SPDX-License-Identifier: GPL-3.0-or-later
// How long an overhead message stays (FUN_00661110, Chat/Chat.cpp).
// The text length is clamped to 254, then the stay is length * 8 + 125
// frames. An empty text makes no node; callers skip it.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::client {

[[nodiscard]] inline int overhead_frames(std::size_t length) {
    const auto shown = std::min(length, std::size_t{254});
    return int(shown * 8 + 125);
}

// The string id an overhead message is drawn as (FUN_004a0e70 walks the
// client's unit tables: players with FUN_004a0b30, monsters and objects
// with FUN_004a0d20). A monster's or object's: atol of the text, drawn
// when 1..0xfffe, else not at all. A player's: 0xff 0xff then an atol in
// 0x42d..0x50f, 0x5586..0x5615 or 0x2aa4; any other text (a number too)
// is drawn as it is (nullopt; overhead_text_shown).
[[nodiscard]] inline std::optional<std::uint16_t> overhead_string(int unit_type, std::string_view text) {
    const auto number_at = [&text](std::size_t from) {
        const std::string digits(text.substr(std::min(from, text.size())));
        return std::strtol(digits.c_str(), nullptr, 10);   // atol: leading spaces and a sign, up to the first non-digit
    };
    if (unit_type == 0) {
        if (text.size() < 2 || text[0] != '\xff' || text[1] != '\xff') return std::nullopt;
        const long number = number_at(2);
        if ((number > 0x42c && number < 0x510) || (number >= 0x5586 && number < 0x5616) || number == 0x2aa4) return std::uint16_t(number);
        return std::nullopt;
    }
    const long number = number_at(0);
    if (number >= 1 && number <= 0xfffe) return std::uint16_t(number);
    return std::nullopt;
}

// Whether a player's text that isn't a string id is drawn (FUN_0049e280,
// called at 0x4a0ce9 with EDI the message's language, 0x26 +2, and the text
// widened as ANSI): a message in language 6, 7, 9 or 0xc from a client of
// another language (7 and 0xc count as one) only when every character is
// under 0x80; any other is drawn. `client_language` is FUN_00525150's.
[[nodiscard]] inline bool overhead_text_shown(int message_language, std::string_view text, int client_language = 0) {
    if (message_language != 6 && message_language != 7 && message_language != 9 && message_language != 0xc) return true;
    if (client_language == message_language) return true;
    if ((client_language == 7 && message_language == 0xc) || (client_language == 0xc && message_language == 7)) return true;
    return std::ranges::none_of(text, [](char letter) { return static_cast<unsigned char>(letter) > 0x7f; });
}

// An overhead box's rect, as FUN_004a0a00 hands FUN_0049e070 (ESI): left,
// top, then left + width and top + height (FUN_0049d3c0's +0x10 / +0x12).
struct OverheadRect { int left = 0, top = 0, right = 0, bottom = 0; };

// IntersectRect (0x6cc4c4) is non-zero when the two overlap by a pixel or
// more; an empty rect meets nothing.
[[nodiscard]] inline bool overhead_rects_meet(const OverheadRect& one, const OverheadRect& other) {
    return std::max(one.left, other.left) < std::min(one.right, other.right) && std::max(one.top, other.top) < std::min(one.bottom, other.bottom);
}

// Where this frame's next overhead box goes (FUN_004a0a00 at 004a0aaa,
// FUN_0049e070): `placed` is the frame's slots (0x7bf0a8, 16 bytes each;
// its count 0x7bf224). With 16 or more placed the caller skips the check
// and draws the box where it is. A box that meets none of them takes the
// next slot as it is. One that meets any is moved: for x steps 0x32..0x15e
// by 0x32, y steps 0x23..0xf5 by 0x23, each of the 8 dwords at 0x722654
// (count at 0x722674) times both steps added to its left and top; the
// first such spot, 400 x 0x118 whatever the box's size, wholly on the W x
// H screen and meeting no slot is stored and drawn at. None: not drawn
// (nullopt), no slot taken. The table reads as four (x, y) pairs, but each
// dword scales both axes (0049e152..0049e165), so only down-right and
// up-left are tried (bugs.md #119; d2d matches).
// A frame's first slots (FUN_004a0e70): the NPC speech box while one is up
// (0x7bf1c0 == 1): x 0x7bf278 = (W - 0x145) / 2, top 0x7225fc = 12, 0x145 by
// 0x70 (npc-talk.md "Box"); none else.
[[nodiscard]] inline std::vector<OverheadRect> overhead_frame_slots(bool speech_up, int screen_w) {
    std::vector<OverheadRect> slots;
    if (speech_up) slots.push_back({ (screen_w - 0x145) / 2, 12, (screen_w - 0x145) / 2 + 0x145, 12 + 0x70 });
    return slots;
}

[[nodiscard]] inline std::optional<OverheadRect> place_overhead(std::vector<OverheadRect>& placed, const OverheadRect& box, int screen_w, int screen_h) {
    static constexpr std::array<int, 8> kSteps = { 1, 1, -1, 1, 1, -1, -1, -1 };
    if (placed.size() >= 16) return box;
    const auto clear = [&placed](const OverheadRect& rect) { return std::ranges::none_of(placed, [&rect](const OverheadRect& slot) { return overhead_rects_meet(slot, rect); }); };
    if (clear(box)) {
        placed.push_back(box);
        return box;
    }
    for (int step_x = 0x32; step_x < 0x190; step_x += 0x32)
        for (int step_y = 0x23; step_y < 0x118; step_y += 0x23)
            for (const int sign : kSteps) {
                const int spot_left = box.left + sign * step_x, spot_top = box.top + sign * step_y;
                const OverheadRect spot{ spot_left, spot_top, spot_left + 0x190, spot_top + 0x118 };
                if (spot.left < 0 || spot.right > screen_w || spot.top < 0 || spot.bottom > screen_h || !clear(spot)) continue;
                placed.push_back(spot);
                return spot;
            }
    return std::nullopt;
}

}  // namespace d2d::client
