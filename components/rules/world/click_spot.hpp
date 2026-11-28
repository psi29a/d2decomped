// SPDX-License-Identifier: GPL-3.0-or-later
// Where a click on the ground walks to (FUN_004623c0, game.exe 1.14d):
// collision.md "What a held frame sends". The client works it out before it
// paths and sends C->S 0x01 / 0x03, in whole subtiles, from the unit's own
// subtile (its path's GetXPos / GetYPos).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>

namespace d2d::rules {

// FUN_00474080: the distance game.exe uses, an octagon's,
// (longer * 0x3d7 + shorter * 0x197) >> 10. It runs a little short (a step of
// 2 straight out is 1).
[[nodiscard]] constexpr int approx_distance(int delta_x, int delta_y) noexcept {
    const int across = delta_x < 0 ? -delta_x : delta_x, down = delta_y < 0 ? -delta_y : delta_y;
    return int(std::uint32_t(std::max(across, down) * 0x3d7 + std::min(across, down) * 0x197) >> 10);
}

// The click's flags, as FUN_00462d00 hands them to FUN_00462a20.
inline constexpr std::uint32_t kClickLeft = 0x1, kClickPress = 0x4, kClickHeld = 0x8, kClickRelease = 0x10, kClickShift = 0x20, kClickRun = 0x40;

// The least distance a click's spot goes out to (LAB_004624a1..LAB_004624d6):
// a press 3, or 5 running; held 4, or 5 running; a release 0.
[[nodiscard]] constexpr int click_least_distance(std::uint32_t flags) noexcept {
    if ((flags & kClickPress) && (flags & kClickRun)) return 5;
    if (flags & kClickHeld) return 4 | int((flags & kClickRun) >> 6);
    if (flags & kClickRelease) return 0;
    return 3;
}

struct ClickSpot { int x = 0, y = 0; };

// The spot a click at (spot_x, spot_y) walks to from (unit_x, unit_y), or
// none: nothing goes when the spot is 0x100 or more away on an axis, or on
// the unit's own subtile (LAB_00462482).
[[nodiscard]] std::optional<ClickSpot> click_spot(int unit_x, int unit_y, int spot_x, int spot_y, std::uint32_t flags) noexcept;

// A player's modes that walk: 2 walk, 3 run, 6 walk in town.
[[nodiscard]] constexpr bool walk_mode(int mode) noexcept { return mode == 2 || mode == 3 || mode == 6; }

// FUN_004623c0's throttle (LAB_00462537, LAB_00462542): a held frame of a
// walking player goes on only once 7 ticks of 0x7a0498 (FUN_0044c790 adds
// one a game tick, 25 a second) have passed since the last that went on;
// anything else goes. What goes stamps the tick (0x462545), a press too.
// The difference is unsigned.
inline constexpr std::uint32_t kHeldWalkTicks = 7;
[[nodiscard]] constexpr bool held_walk_goes(std::uint32_t flags, int player_mode, std::uint32_t tick, std::uint32_t last_tick) noexcept {
    return !(flags & kClickHeld) || !walk_mode(player_mode) || tick - last_tick >= kHeldWalkTicks;
}

// FUN_00462930, a held frame once the press armed it: with a unit under the
// cursor (type, -1 none), an object, missile or item does nothing
// (LAB_0046297f); a player, monster or warp does nothing while the player
// walks (LAB_0046298e..LAB_00462998).
inline constexpr int kUnitWarp = 5;
[[nodiscard]] constexpr bool held_frame_goes(int target_type, int player_mode) noexcept {
    if (target_type < 0) return true;
    if (target_type > 1 && target_type != kUnitWarp) return false;
    return !walk_mode(player_mode);
}

// FUN_00467880's first gate (0x46795f..0x467977): a held frame of the left button
// (flags 8 and 1) without shift (0x20) while the player walks or runs
// (FUN_00622d00, `moving`) runs none of the skill's own searches, whatever the
// skill; everything else goes on to the status test. The right button's held
// frame (flags 8 and 2) isn't gated. tools/emu/search_gate.py.
[[nodiscard]] constexpr bool skill_search_goes(std::uint32_t flags, bool moving) noexcept {
    return !((flags & kClickHeld) && (flags & kClickLeft) && !(flags & kClickShift) && moving);
}

// FUN_00464600 (the local player, its skill): may a press or a held frame send
// its skill now (FUN_004629a0 / FUN_00462a20 / FUN_00462ba0 / FUN_00462c20 drop
// the click on false)? Not with an item in hand or for a non-player; else by the
// mode: SQ (0x12) by its skill's seqinput (FUN_004645b0); tools/emu/press_gate.py.
struct PressIn {
    bool hand_item = false;
    bool player = true;
    int mode = 1;                 // the unit's mode (+0x10); d2d's -1 (stand or walk) as NU
    int char_class = 0;
    bool sq_skill = false;        // the current skill's anim is SQ (row +0x10 0x12)
    int seqinput = 0;             // its Skills.txt seqinput (row +0x16)
    int frame = 0;                // the mode's frame in 256ths (unit +0x38)
};
[[nodiscard]] bool press_goes(const PressIn& press) noexcept;

// FUN_00461dc0's warp (type 5, LAB_00461fb7): refused till 0x7a04c8 (an
// unsigned compare against 0x7a048c, GetTickCount's ms); a click that goes
// sets it 500 ms on (LAB_00462022), as the queue step's 0x13 does
// (LAB_0048168f). Under kWarpReach (FUN_00641530) it sends at once
// (LAB_00462010), else walks and queues 0x13 (LAB_00461fed, LAB_00461ffc).
inline constexpr std::uint32_t kWarpClickWaitMs = 500;
inline constexpr int kWarpReach = 5;
[[nodiscard]] constexpr bool warp_click_goes(std::uint32_t now_ms, std::uint32_t until_ms) noexcept { return now_ms >= until_ms; }

}  // namespace d2d::rules
