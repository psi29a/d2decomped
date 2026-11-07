// SPDX-License-Identifier: GPL-3.0-or-later
// When the belt popup (0x7bef98) is up: its key keeps it up (UI 0x1f), and
// the mouse onto the HUD's belt strip brings it up until the mouse leaves
// the popup. Nothing here draws; Town reads `shown`.
#pragma once

namespace d2d::client::belt_popup {

struct State {
    bool shown = false;     // 0x7bef98: the popup drawn and its boxes live
    bool hovered = false;   // 0x7bef9c: the mouse brought it up
};

// The HUD's belt strip, as FUN_00499bb0 tests it on a `width` x `height`
// screen: x w/2+0x17..w/2+0x91, y h-0x27..h-10.
[[nodiscard]] inline bool on_strip(int mouse_x, int mouse_y, int width, int height) {
    const int half = width / 2;
    return mouse_x >= half + 0x17 && mouse_x <= half + 0x91 && mouse_y >= height - 0x27 && mouse_y <= height - 10;
}

// One frame. `kept` is UI 0x1f (the belt key's toggle), `menu` UI 9 (the
// game menu), `can_hover` the mouse-move handler's gates (FUN_00499bb0:
// a live player, FUN_00463df0 not mode 0x11; d2d: the HUD shown),
// `on_popup` the mouse inside the shown popup's boxes (FUN_00498dc0).
// - UI 0x1f shows it (FUN_00456ee0 -> FUN_00498e90, each frame);
// - shut, the mouse on the strip, the menu not up: shown and hovered;
// - shown, the mouse off the popup: no longer hovered (FUN_00499bb0);
// - neither 0x1f nor hovered, or the menu up: shut (FUN_00499040).
inline void step(State& state, bool kept, bool menu, bool can_hover, bool on_strip, bool on_popup) {
    if (kept) state.shown = true;
    if (can_hover && !menu) {
        if (!state.shown && on_strip) state.shown = state.hovered = true;
        else if (state.shown && !on_popup) state.hovered = false;
    }
    if ((!kept && !state.hovered) || menu) state.shown = state.hovered = false;
}

}  // namespace d2d::client::belt_popup
