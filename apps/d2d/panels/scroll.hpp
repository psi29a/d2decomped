// SPDX-License-Identifier: GPL-3.0-or-later
// The Scroll of Inifuss read (UI 0x10; FUN_0049ff90 opens it, FUN_0049ff10
// draws it): a right-click on bks or bkd in the inventory. FUN_0049fa10
// draws the scroll (UI\MENU\scroin); the deciphered one (bkd) asks the
// server for the stones' order (C->S 0x3e) and, once it's come (S->C 0x50
// subtype 4), fades in its symbols (FUN_0049fba0, rules::stone_symbol).
#pragma once

#include "scene.hpp"
#include "ui.hpp"

#include <stone_scroll.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace d2d::client {

struct ScrollPanel {
    bool open = false;
    bool deciphered = false;                       // bkd (0x7bf228's code)
    std::optional<std::array<int, 5>> order;       // 0x7bf098; none yet: 0x7bf254 still 1, the scroll alone
    d2d::rules::StoneScrollClock clock;            // never reset (0x7bf243 / 0x7bf247)
    // A frame's click: true when it closes the scroll (FUN_0049fea0: a press
    // on the left half, above the control panel).
    bool input(const Mouse& mouse);
    // True when a symbol came this draw (its sound, Sounds.txt 0xa6f).
    bool draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene, std::uint32_t now_ms);
};

}  // namespace d2d::client
