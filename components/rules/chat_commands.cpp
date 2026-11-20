// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of chat_commands.hpp's functions; their notes are in the header.
#include "chat_commands.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace d2d::rules {

// The caller tests the game type (FUN_0044db30: single player or a host)
// and stores the count (FUN_00535780); the line then goes out as "players
// set to N".
std::optional<int> players_command(std::string_view line) {
    constexpr std::string_view kCommand = "/players";
    if (line.size() < kCommand.size()) return std::nullopt;
    for (std::size_t index = 0; index < kCommand.size(); ++index) {
        const char typed = line[index];
        const char lower = typed >= 'A' && typed <= 'Z' ? char(typed - 'A' + 'a') : typed;
        if (lower != kCommand[index]) return std::nullopt;
    }
    std::string_view rest = line.substr(kCommand.size());
    const auto blank = [](char letter) { return letter == ' ' || (letter >= '\t' && letter <= '\r'); };
    while (!rest.empty() && blank(rest.front())) rest.remove_prefix(1);
    bool negative = false;
    if (!rest.empty() && (rest.front() == '+' || rest.front() == '-')) {
        negative = rest.front() == '-';
        rest.remove_prefix(1);
    }
    std::int64_t value = 0;
    for (; !rest.empty() && rest.front() >= '0' && rest.front() <= '9'; rest.remove_prefix(1))
        value = std::min<std::int64_t>(value * 10 + (rest.front() - '0'), std::int64_t{ 1 } << 32);   // past 32 bits it's held anyway
    value = std::clamp<std::int64_t>(negative ? -value : value, INT32_MIN, INT32_MAX);
    return int(std::clamp<std::int64_t>(value, 1, 8));
}

}
