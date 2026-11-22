// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
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

// FUN_00514990's next frontend track, 0..7, marked played. All played:
// the flags clear (FUN_00514860). `restart` (FUN_005148f0 turning the
// playlist on: launch, back from a game) clears them too and picks track
// 0 (0x514a21), introedit / common\options; else `roll` % 8, stepping
// past played tracks.
inline constexpr int frontend_track(std::array<bool, 8>& played, bool& restart, int roll) {
    bool all = true;
    for (const bool done : played) all = all && done;
    if (all || restart) played.fill(false);
    int pick = restart ? 0 : roll % 8;
    restart = false;
    while (played[std::size_t(pick)]) pick = (pick + 1) % 8;
    played[std::size_t(pick)] = true;
    return pick;
}

// LAB_00435cd0 opens character creation when FUN_00430bc0 finds no save.
inline constexpr bool title_single_player_creates(bool has_saves) {
    return !has_saves;
}

}  // namespace d2d::client
