// SPDX-License-Identifier: GPL-3.0-or-later
// How long an overhead message stays (FUN_00661110, Chat/Chat.cpp).
// The text length is clamped to 254, then the stay is length * 8 + 125
// frames. An empty text makes no node; callers skip it.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

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
// is drawn as it is, which this doesn't cover (nullopt).
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

}  // namespace d2d::client
