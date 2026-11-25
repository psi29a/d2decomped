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
#include "thinks/unplaced.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

namespace d2d::rules {

// The think's row, nullptr when d2d has none for that AI.
const ThinkRow* think_row(std::string_view ai_name);

// The AI's think, MonAct::untraced for one d2d has none for.
Think mon_think(std::string_view ai_name, const ThinkIn& input, Rng& rng, const ThinkAway& away);

// An AI d2d thinks for: a row above, or one the game runs itself
// (game/ai/: the prisoners', Baal's throne and stairs, the Dark
// Wanderer's). ImpRide is the riding imp's think (special AI 16), no
// MonStats AI.
bool traced_ai(std::string_view ai_name);

// A special AI's think (the special table 0x73d358 + n x 16, the think at
// +8): 3 a pack's formation, 6 an NPC's melee, 9 a minion's retreat, 12 a
// stationary NPC's (thinks/unplaced.hpp); MonAct::untraced for another.
Think special_think(int special, const ThinkIn& input, Rng& rng, const ThinkAway& away);

// Special AI 3's init (FUN_005e5730): its offset from its leader in the
// scratch words, and a seed step % 100 in the third when it leads.
void formation_init(const ThinkIn& input, Rng& rng);

}  // namespace d2d::rules
