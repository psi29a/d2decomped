// SPDX-License-Identifier: GPL-3.0-or-later
// The body's items as game.exe's hand pickers read them: the item worn as
// a component (FUN_0063c050), the two-melee-weapon test (FUN_005d92d0) and
// the weapon a weapsel 2 skill swings (FUN_00535bc0's case 2: Whirlwind).
// docs/research/re/skills.md "Which hand a skill swings". Checked against
// game.exe's own code by tools/emu/whirl_hands.py (tests/game/test_whirl_hands.cpp).
#pragma once

#include "rules.hpp"

#include <d2s_items.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace d2d::rules {

// One item in a body cell, as the pickers read it.
struct WornItem {
    int cell = 0;           // body cell 1..10 (d2s body_location)
    int class_key = 0;      // FUN_00629fe0: bow 1, 1hs 2, 1ht 3, stf 4, 2hs 5, 2ht 6, xbw 7, ht1 12; 0 none
    int component = 16;     // Items +0x115 (FUN_00628660): 5 right hand, 6 left hand
    bool melee = false;     // ItemTypes row 0x2e (mele), its type or type2 (FUN_00629bb0)
    bool weapon = false;    // ItemTypes row 0x2d (weap)
    bool usable = true;     // FUN_0062a4e0: not flagged 0x100 nor 0x4000
    bool wielded = false;   // the inventory's wielded weapon (FUN_0063bef0)
    int index = -1;         // its place in the items it was read from
};

// FUN_00629fe0's class key for a weapons.txt wclass (the map at 0x7446a0); 0 for one it lacks.
[[nodiscard]] int wclass_key(std::string_view wclass);

// The equipped body items (cells 1..10) in cell order.
[[nodiscard]] std::vector<WornItem> worn_items(const Tables& tables, const std::vector<d2d::d2s::Item>& items);

// FUN_0063c050: the item worn as `component` (5 or 6 for the hands), null none.
[[nodiscard]] const WornItem* worn_by_component(std::span<const WornItem> worn, int component);

// FUN_005d92d0: both hands' components hold a melee weapon, usable or not.
[[nodiscard]] bool two_melee(std::span<const WornItem> worn);

// FUN_00535bc0 for a weapsel 2 skill: a usable weapon the hand pick names, else the first usable weapon
// in hand (FUN_0063c9b0); `left_flag` is the skill node's flag 0x2000. Null none.
[[nodiscard]] const WornItem* weapsel2_pick(std::span<const WornItem> worn, bool two_handed_class, bool left_flag);

}  // namespace d2d::rules
