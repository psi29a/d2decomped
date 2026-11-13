// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>

namespace d2d::font {

// A frontend button's label font (FUN_00500db0, 0x500f7f): by the
// button's height, FontExocet10 (font 9) from 0x23 up, else
// FontRidiculous (font 10), and how far the baseline drops.
struct ButtonLabelFont {
    bool exocet10{};
    int drop{};
};

constexpr ButtonLabelFont button_label_font(int height) {
    if (height >= 0x23) return { true, 4 };
    if (height == 0x20) return { false, 3 };
    return { false, height > 0x14 ? 2 : 1 };
}

struct ButtonLabelSpot {
    int x{};
    int baseline{};
};

// FUN_00500c50: where a label's glyphs sit, bottom-anchored on the
// baseline. `bottom` is the button's bottom row; `font_height` is byte 10
// of the font's .tbl. A two-line button (flag 0x40) puts line 0 at
// bottom - 2 * ((height - font_height) / 3) and line 1 at
// bottom - (height - font_height) / 3; one line sits at
// bottom - (height - font_height) / 2. Pressed: 2 left, 2 down.
// Every halving and third rounds toward zero, as the original's.
constexpr ButtonLabelSpot button_label_spot(int x, int bottom, int width, int height, int label_width,
                                            int font_height, bool pressed, int lines = 1, int line = 0) {
    const int spare = height - font_height;
    int baseline = bottom + button_label_font(height).drop;
    if (lines == 2) baseline -= line == 0 ? (spare / 3) * 2 : spare / 3;
    else            baseline -= spare / 2;
    ButtonLabelSpot spot{ x + std::max(0, (width - label_width) / 2), baseline };
    if (pressed) {
        spot.x -= 2;
        spot.baseline += 2;
    }
    return spot;
}

} // namespace d2d::font
