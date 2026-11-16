// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

namespace d2d::client {

struct TitleVersionBox {
    int x, bottom, width, height;
    int font;
    int alignment;
    std::string_view text;
};

// FUN_004336c0 adds record 0x115 after the title buttons.
inline constexpr TitleVersionBox title_version_box() {
    return { 0, 599, 200, 40, 16, 2, "v 1.14d" };
}

// FUN_004336c0 selects records 9..0xc for classic and 0x10..0x13 for LoD.
inline constexpr std::array<int, 4> title_primary_bottoms(bool expansion) {
    return expansion ? std::array{ 324, 366, 391, 433 }
                     : std::array{ 224, 266, 291, 333 };
}

// LAB_00435cd0 opens character creation when FUN_00430bc0 finds no save.
inline constexpr bool title_single_player_creates(bool has_saves) {
    return !has_saves;
}

}  // namespace d2d::client
