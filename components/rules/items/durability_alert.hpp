// SPDX-License-Identifier: GPL-3.0-or-later
// The worn items' durability alerts: the icons at the screen's top right
// (Panel\DuroIcon) that show while a worn item is at 20 % durability or
// under (FUN_00497fe0, FUN_00497cf0; docs/research/re/ui-module-frontier.md).
// Checked against game.exe's own code by tools/emu/durability_alerts.py
// (tests/test_durability_alerts.cpp).
#pragma once

#include "rules.hpp"

#include <d2s_items.hpp>

#include <array>
#include <vector>

namespace d2d::rules {

// The body slots the alert walks, in its order (0x7223e8): head, torso,
// belt, feet, gloves, right hand, left hand.
inline constexpr std::array<int, 7> kDurabilitySlots{ 1, 3, 8, 9, 10, 4, 5 };

// What the alert asks of one slot's item (all zero: the slot is empty).
struct DurabilitySlot {
    bool item = false;             // something is worn there
    bool has_durability = false;   // rules::has_durability
    bool stackable = false;        // Items.txt stackable
    bool shield = false;           // of type shld (type or type2)
    int quiver = 0;                // its ItemTypes Quiver column, 0 none
    int durability = 0, max_durability = 0;
};

// The slot's facts for `item` (null: empty).
DurabilitySlot durability_slot(const Tables& tables, const d2d::d2s::Item* item);

// One icon: its DuroIcon frame, drawn bottom-left anchored at x, bottom.
struct DurabilityIcon {
    int frame = 0, x = 0, bottom = 0;
};

// The icons to draw, in draw order, for the slots in kDurabilitySlots'
// order; none unless a slot is at 20 % or under. `automap`: UI 10 is up;
// `hostile`: a player is hostile to us.
std::vector<DurabilityIcon> durability_icons(const std::array<DurabilitySlot, 7>& slots, int screen_width, bool automap, bool hostile);

}  // namespace d2d::rules
