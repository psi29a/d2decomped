// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of thinks.hpp's functions; their notes are in the header.
#include "thinks.hpp"

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

const ThinkRow* think_row(std::string_view ai_name) {
    // game.exe indexes its table by the AI row (0x73ca18 + AI x 16); d2d finds
    // the same row by name, linearly (a few hundred compares at a spawn).
    for (const std::span<const ThinkRow> table : { std::span<const ThinkRow>(kAct1Thinks), std::span<const ThinkRow>(kAct2Thinks), std::span<const ThinkRow>(kAct3Thinks),
                                                   std::span<const ThinkRow>(kAct4Thinks), std::span<const ThinkRow>(kAct5Thinks) })
        if (const auto row = std::ranges::find(table, ai_name, &ThinkRow::ai_name); row != table.end()) return &*row;
    return nullptr;
}

Think mon_think(std::string_view ai_name, const ThinkIn& input, Rng& rng, const ThinkAway& away) {
    const auto* row = think_row(ai_name);
    return row ? row->think(ai_name, input, rng, away) : Think{ MonAct::untraced };
}

bool traced_ai(std::string_view ai_name) {
    static constexpr std::array<std::string_view, 4> kGameSide{ "Wussie", "BaalThrone", "BaalToStairs", "DarkWanderer" };
    return (ai_name != "ImpRide" && think_row(ai_name)) || std::ranges::contains(kGameSide, ai_name);
}

}
