// SPDX-License-Identifier: GPL-3.0-or-later
// The Help screen (UI 0x21, docs/research/re/help.md): the key config's
// Help Screen toggles it; at 800x600 FUN_004966c0 draws 800helpborder,
// the close button, the bullets and lines to the control panel, and
// FUN_00495410's labels.
#pragma once

#include "keyconfig.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <cstdint>
#include <vector>

namespace d2d::client {

struct HelpScreen {
    bool open = false;
    bool close_down = false;               // 0x7beefc: the close button held
    // A frame's click; true when it closes (the close button let go over it).
    bool input(const Mouse& mouse);
    void draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const keys::Table& table) const;
};

}  // namespace d2d::client
