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
// game menu), `hud` d2d's HUD shown (local co-op hides it, belt and all),
// `alive` the mouse-move handler's gate (FUN_00499bb0: FUN_00463df0, not
// mode 0x11), `on_popup` the mouse inside the shown popup's boxes
// (FUN_00498dc0).
// - UI 0x1f shows it (FUN_00456ee0 -> FUN_00498e90, each frame);
// - shut, the mouse on the strip, the menu not up: shown and hovered;
// - shown, the mouse off the popup: no longer hovered (FUN_00499bb0);
// - dead: the handler returns first, so a hover stays as it was;
// - the HUD hidden (d2d only): no longer hovered, as no belt is drawn;
// - neither 0x1f nor hovered, or the menu up: shut (FUN_00499040).
inline void step(State& state, bool kept, bool menu, bool hud, bool alive, bool on_strip, bool on_popup) {
    if (!hud) state.hovered = false;
    if (kept) state.shown = true;
    if (hud && alive && !menu) {
        if (!state.shown && on_strip) state.shown = state.hovered = true;
        else if (state.shown && !on_popup) state.hovered = false;
    }
    if ((!kept && !state.hovered) || menu) state.shown = state.hovered = false;
}

// The popup moves the mini-panel left, as a panel on the right does
// (FUN_0047eb50 at 0x47ebbe): UI 0x1f, or the popup drawn (0x7befa0,
// FUN_004979e0), with a belt of more than one row (FUN_004979f0).
// `boxes` is the worn belt's (4 with none).
[[nodiscard]] inline bool moves_mini_panel(const State& state, bool kept, int boxes) {
    return (kept || state.shown) && boxes / 4 > 1;
}

}  // namespace d2d::client::belt_popup
