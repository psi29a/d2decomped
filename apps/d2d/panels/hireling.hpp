// SPDX-License-Identifier: GPL-3.0-or-later
// The Hireling screen (UI 0x24, FUN_004929f0; docs/research/re/hireling.md):
// the merc's items on Panel\NPCInv in the left-panel spot, its name and
// life, level, experience, strength, dexterity, damage, defense and
// resistances; a click takes an item off it or gives it the one in hand.
#pragma once

#include "game_api.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace d2d::client {

struct HirelingPanel {
    bool open = false;
    bool close_down = false;               // 0x7bcea0: the close button held
    // A frame's click: the slot it asks for (cmd::MercItem: 0 give the held
    // item, 1..10 take that one), -2 the panel closes, -1 nothing.
    int input(const Scene& scene, const Mouse& mouse, bool holding);
    void draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const View::MercSheet& sheet, const std::string& name,
              const std::vector<d2d::d2s::Item>& worn, const std::optional<d2d::d2s::Item>& held, int mouse_x, int mouse_y) const;
};

}  // namespace d2d::client
