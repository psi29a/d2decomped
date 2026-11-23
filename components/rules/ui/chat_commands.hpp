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
// to 32 bits) clamped to 1..8. Any other line: none.
std::optional<int> players_command(std::string_view line);

// The players a monster's life, experience (FUN_00573930) and NoDrop
// (FUN_0055a6d0) count (FUN_00535790): those in the game, or the /players
// setting (DAT_00883d70, 0 until set) when it's larger. game.exe takes the
// setting only in a game its own process serves (game +0x6a 1..3: single
// player, a TCP or open-game host), the only games d2d's World runs.
[[nodiscard]] constexpr int game_players(int counted, int setting) { return setting > counted ? setting : counted; }

}  // namespace d2d::rules
