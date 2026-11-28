// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of click_spot.hpp's functions; their notes are in the header.
#include "click_spot.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>

namespace d2d::rules {

// Nearer than click_least_distance (by approx_distance, taken as 1 under
// 2), the step is scaled out to it: the scale is a 32-bit float (FSTP at
// 0x4624e3) and each axis is truncated (FUN_00682fd0, CVTTSD2SI).
std::optional<ClickSpot> click_spot(int unit_x, int unit_y, int spot_x, int spot_y, std::uint32_t flags) noexcept {
    int delta_x = spot_x - unit_x, delta_y = spot_y - unit_y;
    if (std::abs(delta_x) >= 0x100 || std::abs(delta_y) >= 0x100) return std::nullopt;
    if (delta_x == 0 && delta_y == 0) return std::nullopt;
    const int distance = std::max(approx_distance(delta_x, delta_y), 1);
    if (const int least = click_least_distance(flags); distance < least) {
        const float scale = float(double(least) / double(distance));
        delta_x = int(double(delta_x) * double(scale));
        delta_y = int(double(delta_y) * double(scale));
    }
    return ClickSpot{ unit_x + delta_x, unit_y + delta_y };
}

// FUN_00464600: an item in hand (FUN_0063c1e0 on +0x60) or a non-player
// drops it; then the byte table at 0x464684 over the mode (0..0x13, above
// goes): DT, GH, A1, A2, BL, SC, TH, KK, DD and KB never; S1 not for a
// Paladin (class 3), S2 not an Assassin (6), S3 / S4 not a Barbarian, Druid
// or Assassin (4, 5, 6); SQ asks FUN_004645b0: a current skill whose anim
// isn't SQ goes, else its seqinput (0 and 0xff never) at most the frame >> 8,
// an arithmetic shift. NU, WL, RN, TN, TW go.
bool press_goes(const PressIn& press) noexcept {
    if (press.hand_item || !press.player) return false;
    const int mode = press.mode < 0 ? 1 : press.mode;
    switch (mode) {
        case 0: case 4: case 7: case 8: case 9: case 10: case 0xb: case 0xc: case 0x11: case 0x13: return false;
        case 0xd: return press.char_class != 3;
        case 0xe: return press.char_class != 6;
        case 0xf: case 0x10: return press.char_class < 4 || press.char_class > 6;
        case 0x12:
            if (!press.sq_skill) return true;
            if (press.seqinput == 0 || press.seqinput == 0xff) return false;
            return press.seqinput <= (press.frame >> 8);
        default: return true;
    }
}

}
