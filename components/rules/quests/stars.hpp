// SPDX-License-Identifier: GPL-3.0-or-later
// The Arcane Sanctuary's stars: game.exe 1.14d FUN_00476290, which the world
// draw (FUN_00476bc0) calls in level 74 before the lower walls.
// docs/research/re/stars.md. Screen-space: 256 points drift left behind
// everything the world draws.
#pragma once

#include "rules.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace d2d::rules {

struct Stars {
    static constexpr std::size_t kCount = 0x100;
    static constexpr std::uint32_t kStepMs = 0x28;   // a step once more than 40 ms have passed
    struct Star {
        int x = 0, y = 0;                            // 0x7b8d30, 0x7b57e8
        int speed = 0;                               // 0x7b9130: -1..-5 px a step
        std::uint8_t colour = 0;                     // 0x7b5688: a palette index
    };
    // The star seed (*0x712c4c): {1, 0x29a} (FUN_00650e30), then its low
    // word a mix of time() and GetTickCount() (FUN_00650de0) and the high
    // 0x29a (FUN_00650e40): make's `seed`.
    Rng rng;
    std::array<std::uint8_t, 8> colours{};          // 0x7b8d24
    std::array<Star, kCount> stars{};
    std::uint32_t stepped_at = 0;                    // 0x7b57e0
    bool made = false;                               // 0x7b955c: once a run, nothing clears it

    // FUN_00476190: star `index` anywhere across the view (`again` false), or
    // back in at the right edge (true, once it went past the left).
    void place(std::size_t index, bool again, int width, int height) {
        auto& star = stars[index];
        star.x = again ? int(rng.next() & 7) - 1 + width : rng(width);
        star.y = rng(height);
        const std::uint32_t roll = rng.next();
        star.speed = -int(roll % 5) - 1;
        star.colour = colours[rng.next() & 7];
    }

    // The first call (FUN_00476290's head): eight colours from grey 128 to
    // white, each channel ± 32 (capped at 255), as the palette's nearest
    // (FUN_004fb180: `nearest(r, g, b)`), then every star placed.
    // `width` / `height`: 0x7a5220 / 0x7a521c, the screen's width and its height less field_margin
    // (FUN_0044ba20 sets both with the resolution and nothing else writes them: an open side panel
    // doesn't move them; the line slot +0xc0 of the GDI, DirectDraw, Glide and Direct3D tables takes
    // buffer coordinates, bounds-checked against 0x7c9138 / 0x7c913c, with no view offset).
    static constexpr int kFieldMargin = 0x28;
    [[nodiscard]] static constexpr int field_height(int screen_height) { return screen_height - kFieldMargin; }
    template <class Nearest>
    void make(std::uint32_t seed, int width, int height, Nearest&& nearest) {
        rng = Rng(seed);
        for (int shade = 0; shade < 8; ++shade) {
            const int base = shade * 0x80 / 7 + 0x80;
            auto channel = [&] { return std::min(int(rng.next() & 0x3f) - 0x20 + base, 0xff); };
            const int red = channel(), green = channel(), blue = channel();
            colours[std::size_t(shade)] = std::uint8_t(nearest(red, green, blue));
        }
        for (std::size_t index = 0; index < kCount; ++index) place(index, false, width, height);
        made = true;
    }

    // After the frame's draw: when more than 40 ms passed since the last
    // step (GetTickCount), each star moves by its speed; one past the left
    // edge comes back in at the right.
    void step(std::uint32_t now_ms, int width, int height) {
        if (now_ms - stepped_at <= kStepMs) return;
        for (std::size_t index = 0; index < kCount; ++index) {
            stars[index].x += stars[index].speed;
            if (stars[index].x < 0) place(index, true, width, height);
        }
        stepped_at = now_ms;
    }
};

}  // namespace d2d::rules
