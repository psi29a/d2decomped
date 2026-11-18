// SPDX-License-Identifier: GPL-3.0-or-later
// The deciphered Scroll of Inifuss (bkd) read (FUN_0049fba0(1)): the
// server's order (S->C 0x50 subtype 4, FUN_00593cb0: each stone 0..4,
// StoneAlpha..StoneLambda) fades in a numeral a stone, in that order: its
// place in the order (scroin2 frame 0..4, "1".."5") at its stone's spot.
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace d2d::rules {

// 0x7bf243 / 0x7bf247: a step each draw more than 50 ms (0x32) after the
// last. The first draw only starts it. Nothing resets it, so a scroll read
// again goes on where it was.
struct StoneScrollClock {
    std::uint32_t since = 0;
    std::uint32_t steps = 0;
    void draw(std::uint32_t now_ms) {
        if (since == 0) { since = now_ms; steps = 0; return; }
        if (now_ms <= since + 0x32) return;
        since = now_ms;
        ++steps;
    }
};

// One symbol's draw at 800x600 less the left-panel spot (0x7a2858,
// -0x7a285c): scroin2 frame `place` at (x, bottom), `stone`'s, in draw mode `mode`
// (0 / 1 / 2 a quarter, half, three quarters seen; 5 whole), and scroin3
// frame `glow_frame` at (glow_x, glow_bottom), draw mode 3 (additive).
// `sound`: its first step, Sounds.txt 0xa6f (shrine_portal).
struct StoneSymbol { int place = 0, stone = 0, x = 0, bottom = 0, mode = 5, glow_x = 0, glow_bottom = 0, glow_frame = 0; bool sound = false; };

inline constexpr std::array<std::array<int, 2>, 5> kStoneSymbolSpot = { { { 242, 104 }, { 255, 222 }, { 148, 310 }, { 47, 222 }, { 75, 104 } } };   // 0x722eb8, by stone
inline constexpr std::array<std::array<int, 2>, 5> kStoneGlowSpot = { { { 303, 161 }, { 322, 242 }, { 254, 290 }, { 190, 238 }, { 211, 162 } } };    // 0x722ee0, by stone
inline constexpr std::array<std::uint32_t, 5> kStoneSymbolDelay = { 0, 12, 24, 36, 48 };                                                         // 0x722f08, by place in the order
inline constexpr int kStoneSound = 0xa6f;

// The `place`th symbol of the order once `steps` have run; none before its
// delay, or for a stone past 4 (game.exe asserts there).
inline std::optional<StoneSymbol> stone_symbol(const std::array<int, 5>& order, std::size_t place, std::uint32_t steps) {
    if (steps <= kStoneSymbolDelay[place]) return std::nullopt;
    const std::uint32_t age = steps - kStoneSymbolDelay[place];
    const int stone = order[place];
    if (stone < 0 || stone > 4) return std::nullopt;
    const int mode = age < 5 ? 0 : age < 10 ? 1 : age < 15 ? 2 : 5;
    const auto spot = kStoneSymbolSpot[std::size_t(stone)], glow = kStoneGlowSpot[std::size_t(stone)];
    return StoneSymbol{ int(place), stone, spot[0], spot[1], mode, glow[0], glow[1], age > 20 ? 0 : int(age), age == 1 };
}

}  // namespace d2d::rules
