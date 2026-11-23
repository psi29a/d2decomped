// SPDX-License-Identifier: GPL-3.0-or-later
// The hand cursor's animation (FUN_00468310, FUN_004681c0, FUN_004682c0),
// as game.exe plays it while no item or pick is on the cursor.
#pragma once

#include <rules.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace d2d::client {

// The cursor's rows (table 0x712010, 0x1c bytes a row; FUN_004680b0 loads
// DATA\GLOBAL\UI\CURSOR\<name> for each): +0 animated, +4 loops, +8 frames,
// +0xc the step a tick (8.8 frames), +0x10 a press plays ppress from it,
// +0x14 the draw, +0x18 the name.
struct CursorRow {
    const char* name;
    bool animated, loops;
    int frames, step;
    bool pressable;
};
inline constexpr std::array<CursorRow, 7> kCursorRows{ {
    { "Gaunt", false, false, 1, 0, true },
    { "grasp", true, false, 8, 0, true },
    { "ohand", true, false, 8, 0x40, true },
    { "orotate", true, true, 8, 0x20, true },
    { "ppress", true, true, 8, 0x40, true },
    { "protate", true, true, 8, 0x40, true },
    { "buysell", false, false, 1, 0, false },
} };

// Whether a row loops (FUN_00467e40: +4) and its length in 8.8 frames
// (FUN_00467e90: +8 << 8); game.exe asserts the row is under 7.
[[nodiscard]] constexpr bool row_loops(int row) { return kCursorRows[std::size_t(row)].loops; }
[[nodiscard]] constexpr int row_length(int row) { return kCursorRows[std::size_t(row)].frames << 8; }

// The hand's state (0x7a6af0), row (0x7a6adc) and frame (0x7a6ae0, 8.8).
// 1 idle: protate, stepped 0x20 a tick on a draw of the client player's
// unit seed whose low 6 bits are under 0x10. 5 s with no click or move
// (0x7a6ae8): 2, ohand forward, then 4, orotate looping. A move in 2 or 4
// (FUN_00468840): 3, ohand backward from its last frame, then 1. A press
// (FUN_00467f20) on a pressable row: 5, ppress looping; its release
// (FUN_00467fa0): 1. A tick is a draw at least 16 ms (0x10) after the last
// tick (GetTickCount), on an animated row.
struct CursorAnim {
    enum : int { kIdle = 1, kOpening = 2, kClosing = 3, kOpen = 4, kPressed = 5 };
    enum : int { kOhand = 2, kOrotate = 3, kPpress = 4, kProtate = 5 };
    int state = kIdle, row = kProtate, pos = 0;
    std::uint32_t ticked_ms = 0, input_ms = 0;
    // The client player's unit seed in game.exe (unit +0x20); the cursor's
    // own here (deviations.md approximation 21).
    d2d::rules::Rng seed{ 0x2a };

    [[nodiscard]] int frame() const { return pos >> 8; }
    void press(std::uint32_t now_ms) {
        input_ms = now_ms;
        if (state != kPressed && kCursorRows[std::size_t(row)].pressable) set(kPressed, kPpress, 0);
    }
    void release(std::uint32_t now_ms) {
        input_ms = now_ms;
        if (state == kPressed) set(kIdle, kProtate, 0);
    }
    void move(std::uint32_t now_ms) {
        input_ms = now_ms;
        if (state == kOpen || state == kOpening) set(kClosing, kOhand, row_length(kOhand) - 0x100);
    }
    // FUN_00468310, once a draw.
    void tick(std::uint32_t now_ms) {
        if (kCursorRows[std::size_t(row)].animated && ticked_ms + 0x10 < now_ms) {
            ticked_ms = now_ms;
            if (state == kClosing) close_step();
            else open_step();
        }
        if (state == kIdle && input_ms + 5000 < now_ms) set(kOpening, kOhand, 0);
    }

private:
    void set(int to_state, int to_row, int to_pos) { state = to_state; row = to_row; pos = to_pos; }
    // FUN_004681c0.
    void open_step() {
        if (state == kIdle) { if ((seed.next() & 0x3f) < 0x10) pos += 0x20; }
        else pos += kCursorRows[std::size_t(row)].step;
        if (pos < row_length(row)) return;
        if (row_loops(row)) { pos -= row_length(row); return; }
        if (state == kOpening) set(kOpen, kOrotate, 0);
        else set(kIdle, state, 0);   // 5 only (its assert): row 5, protate
    }
    // FUN_004682c0.
    void close_step() {
        pos -= 0x40;
        if (pos >= 0) return;
        if (row_loops(row)) { pos += row_length(row); return; }
        set(kIdle, kProtate, 0);
    }
};

}  // namespace d2d::client
