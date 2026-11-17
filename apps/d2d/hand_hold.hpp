// SPDX-License-Identifier: GPL-3.0-or-later
// A mouse button held on the field, either hand (cursor-units.md "The click's own path").
// game.exe's click map (FUN_00462d00) has a held kind for each button: 1 left
// (FUN_00462a20), 4 right (FUN_00462c20). FUN_0044c000 / FUN_0044c2c0 send one each
// frame the button stays down, and FUN_0044c2c0 only while 0x7a0654 is set: the
// right press (FUN_0044c180) reached the field.
#pragma once

namespace d2d::client::hand_hold {

enum class Held {
    kLatched,   // the attack the press latched goes on, at its monster
    kSkill,     // the hand's skill at the cursor, no unit
    kWalk,      // left only: the walk re-aims (FUN_004623c0)
};

// What a held frame sends. The left hand casts in place only with Stand Still (0x7a0664);
// the right never walks (FUN_00462c20 has no ground-walk branch).
[[nodiscard]] constexpr Held held(bool left, bool latched_alive, bool stand_still) {
    if (latched_alive) return Held::kLatched;
    return left && !stand_still ? Held::kWalk : Held::kSkill;
}

// The right button's 0x7a0654 after this frame: a press sets it to whether the field took it
// (not over the UI); letting go clears it; otherwise it stays.
[[nodiscard]] constexpr bool right_latch(bool latched, bool press, bool down, bool over_ui) {
    if (press) return !over_ui;
    return down && latched;
}

}  // namespace d2d::client::hand_hold
