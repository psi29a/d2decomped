// SPDX-License-Identifier: GPL-3.0-or-later
// The small, data-driven layout of game.exe's connecting screen.
#pragma once

#include <cstdint>

namespace d2d::client {

struct ConnectingScreen {
    int box_record = 0, cancel_record = 0, text_record = 0;
    int box_x = 0, box_bottom = 0, box_width = 0, box_height = 0;
    int text_x = 0, text_bottom = 0, text_width = 0, text_height = 0;
    int font = 0, alignment = 0;
    std::uint16_t string_id = 0;
};

// FUN_0042f6e0: both palette branches make a popup, a hidden CANCEL and a
// Font24 text record, then set the text to string 0x147b ("PLEASE WAIT").
constexpr ConnectingScreen connecting_screen(bool character_palette) {
    return character_palette
        ? ConnectingScreen{ 0xf7, 0xfa, 0xf9, 268, 350, 264, 176, 268, 320, 264, 120, 24, 2, 0x147b }
        : ConnectingScreen{ 0xde, 0xdf, 0xe0, 268, 350, 264, 176, 268, 300, 264, 100, 24, 2, 0x147b };
}

}  // namespace d2d::client
