// Rain — game.exe 1.14d, the client's weather (0x472320..0x473fc3).
// docs/research/re/weather.md. Screen-space: drops fall across the view
// and the world scrolls under them.
// ponytail: Act 1's rain only; Act 5's snow, the mud's bubbles and the
// lightning a skill sets off (FUN_00472c50) aren't here.
#pragma once

#include <rules.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace d2d::rules {

// game.exe's sine table (0x707800): 512 steps a turn; cos is 128 on.
inline float sin512(int a) { return float(std::sin(double(a & 511) * 6.283185307179586 / 512)); }
inline float cos512(int a) { return sin512(a + 128); }

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
    Drop drop(Rng& r, int w, int h, int day_type) {
        Drop d;
        d.x = r(w);
        d.bottom = 40 + r(h - 87);
        d.y = -20 + r(d.bottom + 20);
        r.next();                                  // the snow's sway phase
        d.tone = r(12);
        d.kind = day_type;
        const float f = float(d.bottom - 40) / float(h - 87);
        d.len = 4 - int(f * -8.0);
        d.speed = 15 - int(f * -15.0);
        return d;
    }

    // One client frame (FUN_00473f50): the drops fall, the splashes play,
    // the cycle moves on, drops fill up to the density and the wind turns.
    // (sx, sy): how far the view scrolled this frame, pixels.
    void tick(Rng& r, int w, int h, int sx, int sy, int day_type) {
        const float pace = volume() * 0.15f + 0.85f;
        for (auto& d : drops) {                    // FUN_004732c0
            const auto v = float(int(float(d.speed) * pace));
            int dx = int(cos512(wind) * v);
            const int dy = int(sin512(wind) * v);
            d.y += dy - sy;
            if (d.y > d.bottom) d.landed = true;
            if (d.landed) {
                if (d.wait == 0) {
                    if (int(drops.size()) <= density) d = drop(r, w, h, day_type);
                    else d.speed = -1;             // gone
                    continue;
                }
                --d.wait; d.speed = 0; dx = 0;
            }
            d.x += dx - sx;
            if (d.x >= w) d.x -= w;
            if (d.x < 0) d.x += w;
        }
        std::erase_if(drops, [](const Drop& d) { return d.speed < 0; });
        for (auto& s : splashes) { s.x -= sx; s.y -= sy; if (--s.wait < 1) { ++s.frame; s.wait = 2; } }   // FUN_00472c80
        std::erase_if(splashes, [&](const Splash& s) { return s.frame >= splash_frames[std::size_t(s.kind)]; });

        if (left == 0) {                           // FUN_00473e50
            state = (state + 1) % 4;
            length = left = kBase[std::size_t(state)] + r(kSpread[std::size_t(state)]);
            if (state == 0) density = 0;
            if (state == 1) {                      // FUN_00472610: the wind, then how hard
                wind_left = r(0x177) + 0x7d;
                r.next();                          // the lightning's delay
                wind = wind_to = r(0x47) + 0x5c;
                target = 0x20 + r(0xe0);
            }
            if (state == 2) { density = target; r.next(); }
        }
        --left;
        if (state == 1) density = int(std::int64_t(length - left) * target / length);
        if (state == 3) density = target * left / length;

        while (int(drops.size()) < density) drops.push_back(drop(r, w, h, day_type));   // FUN_004737b0
        if (wind_to < wind) wind = std::max(wind - 2, wind_to);
        else if (wind < wind_to) wind = std::min(wind + 2, wind_to);
        if (--wind_left == 0) { wind_left = r(0x177) + 0x7d; wind_to = r(0x47) + 0x5c; }
    }

    // A floor tile whose DT1 material flags have 2 drawn with its top
    // corner at (x, y) (FUN_004de410): rand(1000) under the density puts a
    // splash in it (FUN_00472da0). game.exe bug (bugs.md #10): the spot runs
    // from the top corner rightward by the row's width, so splashes sit half
    // a tile right.
    void floor(Rng& r, int x, int y) {
        if (r(1000) >= density) return;
        const int row = r(80), half = row < 40 ? 40 - row : row - 40;
        Splash s;
        s.x = r((80 - 2 * half) * 2) + x;
        s.y = row + y;
        s.kind = int(r.next() & 1) + 2;
        splashes.push_back(s);
    }
};

// The drops' colour by the time of day when they fell (FUN_00472890:
// nearest palette colours; day ones drawn half see-through) — tone 0..11.
inline std::array<std::uint8_t, 3> rain_rgb(int kind, int tone) {
    if (kind == 0) { const int c = tone * 80 / 12; return { std::uint8_t(98 - c), std::uint8_t(123 - c), std::uint8_t(98 - c) }; }
    if (kind == 2) { const int c = tone * 2; return { std::uint8_t(25 - c), std::uint8_t(30 - c), std::uint8_t(25 - c) }; }
    const int c = tone * 40 / 12;
    return { std::uint8_t(45 - c), std::uint8_t(55 - c), std::uint8_t(45 - c) };
}

}  // namespace d2d::rules
