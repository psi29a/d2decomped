// SPDX-License-Identifier: GPL-3.0-or-later
// Stat and skill points, respec, mana regeneration and a healer's heal.
// The tables these read are rules.hpp's.
#pragma once

#include "rules.hpp"

#include <d2s.hpp>
#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::rules {

// game.exe's MulDiv: value x times / over, truncated, 0 over 0.
int mul_div(int value, int times, int over);

// The player's mana a frame, in its 32-bit math.
std::int64_t mana_per_frame(std::int64_t max_mana, int regen_seconds, int bonus_pct, int flat, bool no_regen = false);

// Spends up to n unspent stat points (stat 4) on stat (0 strength, 1
// energy, 2 dexterity, 3 vitality), as the char panel's buttons ask with
// packet 0x3a (FUN_004a78c0). Vitality raises life and stamina, energy
// mana, current and max (8.8 fixed: a quarter point is 64). Returns the
// points spent.
int spend_stat_points(d2d::d2s::Stats& stats, int stat, int count, const ClassGains& gains);

// Akara's Reset Stat/Skill Points.
void respec(d2d::d2s::Stats& stats, const std::array<int, 4>& base, const ClassGains& gains);

// Can skill i (0..29 of class cls) take a point: a level to go, the character level, and every prerequisite
// learned?
bool can_learn(const Tables& tables, int cls, int skill_index, const std::array<std::uint8_t, 30>& levels, int clvl);

// Spends a skill point (stat 5) on skill i. Returns whether it did.
bool learn_skill(const Tables& tables, int cls, int skill_index, std::array<std::uint8_t, 30>& levels, d2d::d2s::Stats& stats);

// Healers restore life and mana when you talk to them: Akara, Atma, Fara, Ormus, Jamella, Malah.
bool is_healer(int hc_idx);

void heal(d2d::d2s::Stats& stats);

}  // namespace d2d::rules
