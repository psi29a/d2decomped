// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>

namespace d2d::rules {

// The chat box's "/players N" (FUN_0047c420 at 0x47c4a8..0x47c4fa;
// docs/research/re/chat-commands.md "/players"): the line past its leading
// blanks starts with "/players" in any case (_strnicmp, 8 characters), and
// the count is MSVC's atol of what follows (blanks, a sign, the digits, held
// to 32 bits) clamped to 1..8. Any other line: none. The caller tests the
// game type (FUN_0044db30: single player or a host) and stores the count
// (FUN_00535780); the line then goes out as "players set to N".
inline std::optional<int> players_command(std::string_view line) {
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

// The players a monster's life, experience (FUN_00573930) and NoDrop
// (FUN_0055a6d0) count (FUN_00535790): those in the game, or the /players
// setting (DAT_00883d70, 0 until set) when it's larger. game.exe takes the
// setting only in a game its own process serves (game +0x6a 1..3: single
// player, a TCP or open-game host), the only games d2d's World runs.
[[nodiscard]] constexpr int game_players(int counted, int setting) { return setting > counted ? setting : counted; }

}  // namespace d2d::rules
