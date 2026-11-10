// SPDX-License-Identifier: GPL-3.0-or-later
// The MonAI thinks by MonStats AI name (thinks/: a file per act, each
// think a member function, its row in that act's table). game.exe's
// table (0x73ca18 + AI x 16) holds the think at +8; d2d keys it by the
// MonAI.txt row's name, which MonStats' AI column gives.
#pragma once

#include "monsters.hpp"
#include "rules.hpp"
#include "thinks/act1.hpp"
#include "thinks/act2.hpp"
#include "thinks/act3.hpp"
#include "thinks/act4.hpp"
#include "thinks/act5.hpp"
#include "thinks/thinker.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

namespace d2d::rules {

// The think's row, nullptr when d2d has none for that AI.
inline const ThinkRow* think_row(std::string_view ai_name) {
    // ponytail: a linear find, as the if-chain before it; a sorted table if it ever shows in a profile.
    for (const std::span<const ThinkRow> table : { std::span<const ThinkRow>(kAct1Thinks), std::span<const ThinkRow>(kAct2Thinks), std::span<const ThinkRow>(kAct3Thinks),
                                                   std::span<const ThinkRow>(kAct4Thinks), std::span<const ThinkRow>(kAct5Thinks) })
        if (const auto row = std::ranges::find(table, ai_name, &ThinkRow::ai_name); row != table.end()) return &*row;
    return nullptr;
}

// The AI's think, MonAct::untraced for one d2d has none for.
inline Think mon_think(std::string_view ai_name, const ThinkIn& input, Rng& rng, const ThinkAway& away) {
    const auto* row = think_row(ai_name);
    return row ? row->think(ai_name, input, rng, away) : Think{ MonAct::untraced };
}

// An AI d2d thinks for: a row above, or one the game runs itself
// (game/ai.cpp: the prisoners', Baal's throne and stairs, the Dark
// Wanderer's). ImpRide is the riding imp's think (special AI 16), no
// MonStats AI.
inline bool traced_ai(std::string_view ai_name) {
    static constexpr std::array<std::string_view, 4> kGameSide{ "Wussie", "BaalThrone", "BaalToStairs", "DarkWanderer" };
    return (ai_name != "ImpRide" && think_row(ai_name)) || std::ranges::contains(kGameSide, ai_name);
}

}  // namespace d2d::rules
