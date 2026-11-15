// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The chat box's "/players N" (rules::players_command, FUN_0047c420) and
// the count monsters and NoDrop use (rules::game_players, FUN_00535790),
// against game.exe's own code (tools/emu/chat_parse.py --inc writes the
// cases): the lines it runs in single player, and settings 0..9 against
// 0..9 players in a game it serves.
#include <chat_commands.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <iterator>
#include <string>
#include <string_view>

int main() try {
    struct LineCase { std::string_view line; int stored; std::string_view sent; };
    struct CountCase { int setting, players, count; };
#include "chat_players_cases.inc"
    int differ = 0;
    for (const auto& sample : kLines) {
        const auto count = d2d::rules::players_command(sample.line);
        const bool same = sample.stored < 0 ? !count : count && *count == sample.stored && sample.sent == "players set to " + std::to_string(*count);
        if (!same) {
            std::printf("differ: \"%.*s\": game.exe %d, d2d %d\n", int(sample.line.size()), sample.line.data(), sample.stored, count ? *count : -1);
            ++differ;
        }
    }
    for (const auto& sample : kCounts) {
        const int count = d2d::rules::game_players(sample.players, sample.setting);
        if (count != sample.count) {
            std::printf("differ: setting %d, %d players: game.exe %d, d2d %d\n", sample.setting, sample.players, sample.count, count);
            ++differ;
        }
    }
    std::printf("%zu lines, %zu counts, %d differ\n", std::size(kLines), std::size(kCounts), differ);
    assert(differ == 0);
    return differ == 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::printf("error: %s\n", error.what());
    return 1;
}
