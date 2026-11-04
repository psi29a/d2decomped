// SPDX-License-Identifier: GPL-3.0-or-later
// The mini-panel's (UI 0x15) rules that draw nothing; gamemenu.hpp's
// MiniPanel draws it and takes its clicks.
#pragma once

#include <cstdlib>
#include <string>

namespace d2d::client::mini_panel {

// d2d.cfg's mini_panel (game.exe: the registry's "Mini Panel"). At a game's
// end FUN_0047f130 writes 1 when it was shut as the game menu last opened
// (0x71315c), else 0; FUN_004567f0 opens it with the next game unless the
// value reads nonzero.
[[nodiscard]] inline const char* saved(bool open_at_menu) { return open_at_menu ? "0" : "1"; }
[[nodiscard]] inline bool opens(const std::string& saved) { return std::strtoul(saved.c_str(), nullptr, 0) == 0; }

}  // namespace d2d::client::mini_panel
