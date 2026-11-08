// SPDX-License-Identifier: GPL-3.0-or-later
// The view with side panels open: FUN_00455f20 / FUN_00456300 set it as a
// UI opens or shuts (FUN_0045aea0), FUN_0045ae90 reads it.
// docs/research/re/menu.md "The view with panels open".
#pragma once

namespace d2d::rules {

namespace panel_view {
inline constexpr int kWhole = 0;      // nothing at the sides
inline constexpr int kRightOpen = 1;  // the inventory or the skill tree (UI 1, 4)
inline constexpr int kLeftOpen = 2;   // the character, quests, waypoints, party, hireling... (UI 2, 0xf, 0x14, 0x16, 0x24)
inline constexpr int kCovered = 3;    // both sides, or a store, the stash, the cube (UI 0xc, 0x19, 0x1a)
}  // namespace panel_view

// The state from what's open: `left` a left panel, `right` a right one,
// `covering` a UI that takes both halves.
constexpr int panel_view_state(bool left, bool right, bool covering) {
    if (covering || (left && right)) return panel_view::kCovered;
    return right ? panel_view::kRightOpen : left ? panel_view::kLeftOpen : panel_view::kWhole;
}

// How far left of the screen's middle the world draws its centre: +W/4 with
// a right panel open, so the player stands in the middle of the left half;
// -W/4 with a left one (0x7a5214, negated; FUN_0045afc0 / FUN_0045aff0 take
// it off the view's left both ways). The view's rect moves with it
// (FUN_00476000 on 0x7a0640: {-W/4, 0, W - W/4, H - 40}, or {W/4, ...}).
constexpr int panel_view_shift(int state, int width) {
    return state == panel_view::kRightOpen ? width / 4 : state == panel_view::kLeftOpen ? -(width / 4) : 0;
}

// The cursor's x after a key (or Esc's close-all) took the view from
// `before` to `after`, as FUN_00455f20 / FUN_00456300 move it when their
// flag is set (FUN_00468770, SetCursorPos; y stays). `x` and `width` are
// the view's. It keeps to the same spot of the world, unless that's under
// the panel or off the half it comes back from:
// - a right panel opens: x - W/4 when x > W/4; it shuts: x + W/4 when x < W/2;
// - a left one opens: x + W/4 when x < W - W/4; it shuts: x - W/4 when x > W/2.
// Both sides (3, a store / stash / cube aside) to none is Esc's: it shuts
// the inventory and tree first, so the left's rule. Any other step: none.
constexpr int panel_view_cursor_x(int before, int after, int x, int width) {
    const int quarter = width / 4;
    if (after == panel_view::kRightOpen && before == panel_view::kWhole) return x > quarter ? x - quarter : x;
    if (after == panel_view::kLeftOpen && before == panel_view::kWhole) return x < width - quarter ? x + quarter : x;
    if (after != panel_view::kWhole) return x;
    if (before == panel_view::kRightOpen) return x < width / 2 ? x + quarter : x;
    if (before == panel_view::kLeftOpen || before == panel_view::kCovered) return x > width / 2 ? x - quarter : x;
    return x;
}

}  // namespace d2d::rules
