// SPDX-License-Identifier: GPL-3.0-or-later
// The mini-panel's (UI 0x15) rules that draw nothing; gamemenu.hpp's
// MiniPanel draws it and takes its clicks.
#pragma once

#include <array>
#include <cstdlib>
#include <span>
#include <string>

namespace d2d::client::mini_panel {

// The bar's two forms (FUN_0047f0c0): game type 0, single player, takes
// Panel\minipanel_s and 7 buttons (0x7bc978 = 1); any other type, a joined
// or hosted game, takes the wide Panel\minipanel and 8, button 3 the Party
// screen (FUN_0047ec50 case 3). `party` below is that wide form.
inline constexpr std::array<int, 8> kWideButtons = { 0, 1, 2, 3, 4, 5, 6, 7 };
inline constexpr std::array<int, 7> kSingleButtons = { 0, 1, 2, 4, 5, 6, 7 };

// The buttons left to right, each its action and frame / 2 (FUN_0047e8b0).
[[nodiscard]] inline std::span<const int> buttons(bool party) {
    return party ? std::span<const int>(kWideButtons) : std::span<const int>(kSingleButtons);
}

// The cels' bottoms: the bar's (FUN_0047f710) and the buttons' (FUN_0047e8b0).
[[nodiscard]] inline int bar_y(int height) { return height - 0x2f; }
[[nodiscard]] inline int button_y(int height) { return height - 0x32; }

// The first button's x (FUN_0047e8b0, 0x15 apart): beside a right-hand
// panel, beside a left-hand one, or centred.
[[nodiscard]] inline int first_button_x(int width, bool left, bool right, bool party) {
    if (right) return width / 2 - 0xca;
    if (left) return width / 2 + (party ? 0x23 : 0x39);
    return width / 2 - (party ? 0x54 : 0x4a);
}

// The bar's x (FUN_0047f710's FUN_004f6480 call).
[[nodiscard]] inline int bar_x(int width, bool left, bool right, bool party) {
    if (right) return width / 2 - 0xcd;
    if (left) return width / 2 + (party ? 0x23 : 0x38) - 2;
    return width / 2 - (party ? 0x54 : 0x4a) - 3;
}

// d2d.cfg's mini_panel (game.exe: the registry's "Mini Panel"). At a game's
// end FUN_0047f130 writes 1 when it was shut as the game menu last opened
// (0x71315c), else 0; FUN_004567f0 opens it with the next game unless the
// value reads nonzero.
[[nodiscard]] inline const char* saved(bool open_at_menu) { return open_at_menu ? "0" : "1"; }
[[nodiscard]] inline bool opens(const std::string& saved) { return std::strtoul(saved.c_str(), nullptr, 0) == 0; }

// Its Messages button (5, 0x47ed08): the belt (UI 0x1f) shut, the Message
// Log (UI 0x18) toggled.
inline void messages(bool& belt_open, bool& message_log) { belt_open = false; message_log = !message_log; }

}  // namespace d2d::client::mini_panel
