// SPDX-License-Identifier: GPL-3.0-or-later
// Rain — game.exe 1.14d, the client's weather (0x472320..0x473fc3).
// docs/research/re/weather.md. Screen-space: drops fall across the view
// and the world scrolls under them.
// ponytail: Act 1's rain only; Act 5's snow, the mud's bubbles and the
// lightning a skill sets off (FUN_00472c50) aren't here.
#pragma once

#include "rules.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace d2d::rules {

// game.exe's sine table (0x707800): 512 steps a turn; cos is 128 on.
inline float sin512(int angle) { return float(std::sin(double(angle & 511) * 6.283185307179586 / 512)); }
inline float cos512(int angle) { return sin512(angle + 128); }

struct Rain {
    // The cycle (FUN_00473e50): 0 clear, 1 rising, 2 full, 3 falling, each
    // base + rand(spread) ticks (0x7a8970 / 0x7a8958). The first tick of
    // a game after launch starts it rising.
    static constexpr std::array<int, 4> kBase{ 7500, 250, 3000, 125 }, kSpread{ 7500, 250, 3000, 50 };
    int state = 0, left = 0, length = 1;
    int target = 0, density = 0;                   // drops when full (32..255); now
    int wind = 0x7f, wind_to = 0x7f, wind_left = 0x138;   // the drops' angle, 512ths (128 straight down)
    struct Drop {
        int x = 0, y = 0, bottom = 0;              // screen; where it lands
        int len = 0, speed = 0, wait = 3;          // nearer ones longer and faster; ticks it shows landed
        bool landed = false;
        int tone = 0;                              // 0..11 of the time of day's colours
        int kind = 0;                              // the time of day when it fell (0 day, 1 dusk, 2 night, 3 dawn)
    };
    std::vector<Drop> drops;
    struct Splash { int x = 0, y = 0, kind = 2, frame = 0, wait = 2; };   // Rain3 / Rain4 (kind 2 / 3)
    std::vector<Splash> splashes;
    std::array<int, 4> splash_frames{};            // Rain1..4's frame counts: a splash ends past its last
    Rng rng{ 1 };                                  // game.exe rolls the player unit's seed (unit +0x20)

    [[nodiscard]] float volume() const { return float(density) * (1.f / 256); }   // 0x7a89a0

    // A new drop somewhere above where it lands (FUN_00473090).
    Drop drop(Rng& roll, int width, int height, int day_type) {
        Drop created;
        created.x = roll(width);
        created.bottom = 40 + roll(height - 87);
        created.y = -20 + roll(created.bottom + 20);
        roll.next();                                  // the snow's sway phase
        created.tone = roll(12);
        created.kind = day_type;
        const float fraction = float(created.bottom - 40) / float(height - 87);
        created.len = 4 - int(fraction * -8.0);
        created.speed = 15 - int(fraction * -15.0);
        return created;
    }

    // One client frame (FUN_00473f50): the drops fall, the splashes play,
    // the cycle moves on, drops fill up to the density and the wind turns.
    // (sx, sy): how far the view scrolled this frame, pixels.
    void tick(Rng& roll, int width, int height, int shift_x, int shift_y, int day_type) {
        const float pace = volume() * 0.15f + 0.85f;
        for (auto& raindrop : drops) {                    // FUN_004732c0
            const auto speed = float(int(float(raindrop.speed) * pace));
            int dx = int(cos512(wind) * speed);
            const int dy = int(sin512(wind) * speed);
            raindrop.y += dy - shift_y;
            if (raindrop.y > raindrop.bottom) raindrop.landed = true;
            if (raindrop.landed) {
                if (raindrop.wait == 0) {
                    if (int(drops.size()) <= density) raindrop = drop(roll, width, height, day_type);
                    else raindrop.speed = -1;             // gone
                    continue;
                }
                --raindrop.wait; raindrop.speed = 0; dx = 0;
            }
            raindrop.x += dx - shift_x;
            if (raindrop.x >= width) raindrop.x -= width;
            if (raindrop.x < 0) raindrop.x += width;
        }
        std::erase_if(drops, [](const Drop& raindrop) { return raindrop.speed < 0; });
        for (auto& splash : splashes) { splash.x -= shift_x; splash.y -= shift_y; if (--splash.wait < 1) { ++splash.frame; splash.wait = 2; } }   // FUN_00472c80
        std::erase_if(splashes, [&](const Splash& splash) { return splash.frame >= splash_frames[std::size_t(splash.kind)]; });

        if (left == 0) {                           // FUN_00473e50
            state = (state + 1) % 4;
            length = left = kBase[std::size_t(state)] + roll(kSpread[std::size_t(state)]);
            if (state == 0) density = 0;
            if (state == 1) {                      // FUN_00472610: the wind, then how hard
                wind_left = roll(0x177) + 0x7d;
                roll.next();                          // the lightning's delay
                wind = wind_to = roll(0x47) + 0x5c;
                target = 0x20 + roll(0xe0);
            }
            if (state == 2) { density = target; roll.next(); }
        }
        --left;
        if (state == 1) density = int(std::int64_t(length - left) * target / length);
        if (state == 3) density = target * left / length;

        while (int(drops.size()) < density) drops.push_back(drop(roll, width, height, day_type));   // FUN_004737b0
        if (wind_to < wind) wind = std::max(wind - 2, wind_to);
        else if (wind < wind_to) wind = std::min(wind + 2, wind_to);
        if (--wind_left == 0) { wind_left = roll(0x177) + 0x7d; wind_to = roll(0x47) + 0x5c; }
    }

    // A floor tile whose DT1 material flags have 2 drawn with its top
    // corner at (x, y) (FUN_004de410): rand(1000) under the density puts a
    // splash in it (FUN_00472da0). game.exe bug (bugs.md #10): the spot runs
    // from the top corner rightward by the row's width, so splashes sit half
    // a tile right.
    void floor(Rng& roll, int x, int y) {
        if (roll(1000) >= density) return;
        const int row = roll(80), half = row < 40 ? 40 - row : row - 40;
        Splash splash;
        splash.x = roll((80 - 2 * half) * 2) + x;
        splash.y = row + y;
        splash.kind = int(roll.next() & 1) + 2;
        splashes.push_back(splash);
    }
};

// The drops' colour by the time of day when they fell (FUN_00472890:
// nearest palette colours; day ones drawn half see-through) — tone 0..11.
inline std::array<std::uint8_t, 3> rain_rgb(int kind, int tone) {
    if (kind == 0) { const int shade = tone * 80 / 12; return { std::uint8_t(98 - shade), std::uint8_t(123 - shade), std::uint8_t(98 - shade) }; }
    if (kind == 2) { const int shade = tone * 2; return { std::uint8_t(25 - shade), std::uint8_t(30 - shade), std::uint8_t(25 - shade) }; }
    const int shade = tone * 40 / 12;
    return { std::uint8_t(45 - shade), std::uint8_t(55 - shade), std::uint8_t(45 - shade) };
}

}  // namespace d2d::rules
