// SPDX-License-Identifier: GPL-3.0-or-later
// Rain and snow — game.exe 1.14d, the client's weather (0x472320..0x473fc3).
// docs/research/re/weather.md. Screen-space: drops fall across the view
// and the world scrolls under them.
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
    // base + rand(spread) ticks (0x7a8970 / 0x7a8958; full's base tripled
    // for a game joined in Act 3 or 5, FUN_00472890). The first tick of a
    // game after launch starts it rising.
    static constexpr std::array<int, 4> kBase{ 7500, 250, 3000, 125 }, kSpread{ 7500, 250, 3000, 50 };
    int full_base = 3000;
    int state = 0, left = 0, length = 1;
    int target = 0, density = 0;                   // drops when full (32..255); now
    int wind = 0x7f, wind_to = 0x7f, wind_left = 0x138;   // the drops' angle, 512ths (128 straight down)
    // Act 5's snow (0x7a8a14) for its rain levels, flakes a size bigger
    // (0x7a8a18: 117, 120, 121), the cycle held full (0x7a8a1c: 120, 121);
    // the snow wind's next angle: low + rand(span) (0x7a89d8 / 0x7a8998).
    bool snow = false, big = false, held = false;
    int wind_low = 0x2a, wind_span = 0xaa;
    int sway = 0;                                  // 0x7a8a34: frames, the flakes' sway
    // Lightning (0x7a8a08), set off by a Thunder Storm strike (its client do
    // 84, FUN_004e3c50 -> FUN_00472c50): draw frames of a flash left
    // (0x7a89e8), the strike's second flash next (0x7a896c), ticks to the
    // next strike (0x7a8968), thunder (0x712b4c: not the first strike's).
    bool lightning = false, second = false, heard = false;
    int flash = 0, strike_left = 0x4e2;
    bool flashing = false;                         // this drawn frame is a flash's
    struct Drop {
        int x = 0, y = 0, bottom = 0;              // screen; where it lands
        int len = 0, speed = 0, wait = 3;          // nearer ones longer and faster; ticks it shows landed
        bool landed = false;
        int tone = 0;                              // 0..11 of the time of day's colours
        int kind = 0;                              // the time of day when it fell (0 day, 1 dusk, 2 night, 3 dawn)
        int phase = 0, size = 0;                   // a flake's sway phase (512ths) and shape (0..7)
    };
    std::vector<Drop> drops;
    struct Splash { int x = 0, y = 0, kind = 2, frame = 0, wait = 2; };   // Rain3 / Rain4 (kind 2 / 3)
    std::vector<Splash> splashes;
    std::array<int, 4> splash_frames{};            // Rain1..4's frame counts: a splash ends past its last
    // A Mud level's bubbles (bubble1..4, kind 2 made: bubble3), as splashes.
    std::vector<Splash> bubbles;
    std::array<int, 4> bubble_frames{};
    // The floor pass's gates (FUN_004de730): the client frame (0x7a0498),
    // the frames of the last splash and bubble passes, this pass's.
    int clock = 0, splash_at = 0, bubble_at = 0, threshold = 0;
    bool splash_pass = false, bubble_pass = false;
    Rng rng{ 1 };                                  // game.exe rolls the player unit's seed (unit +0x20)
    // The palette indices FUN_00472890 makes at the join (FUN_004fb180):
    // rain by day, dusk / dawn, night (rain_rgb), a flake's grey (snow_rgb).
    std::array<std::array<std::uint8_t, 12>, 4> index{};

    [[nodiscard]] float volume() const { return float(density) * (1.f / 256); }   // 0x7a89a0

    // A new drop or flake somewhere above where it lands (FUN_00473090).
    Drop drop(Rng& roll, int width, int height, int day_type) const {
        Drop created;
        created.x = roll(width);
        created.bottom = 40 + roll(height - 87);
        created.y = -20 + roll(created.bottom + 20);
        created.phase = int(roll.next() & 0x1ff);
        created.kind = day_type;
        const float fraction = float(created.bottom - 40) / float(height - 87);
        if (snow) {
            roll.next();
            created.wait = 1;
            created.speed = 8 - int(double(fraction) * -28.0);
            created.size = std::clamp((created.bottom - 40) * 7 / (height - 87) + (big ? 1 : 0), 0, 7);
            return created;
        }
        created.tone = roll(12);
        created.len = 4 - int(double(fraction) * -8.0);
        created.speed = 15 - int(double(fraction) * -15.0);
        return created;
    }

    // The wind and the lightning's delay (FUN_00472610: three draws on the
    // seed; the angle 92..162).
    void winds(Rng& roll) {
        wind_left = roll(0x177) + 0x7d;
        strike_left = roll(0x5dc) + 500;
        wind = wind_to = roll(0x47) + 0x5c;
    }
    // A Thunder Storm strike (FUN_00472c50, ECX 0): lightning if there's none.
    void set_off() {
        if (lightning) return;
        lightning = true; flash = 1; second = false; heard = false;
    }
    // A drawn frame (FUN_00473910): with a flash due it's drawn (white over
    // the view, as the rain's spot test's area; the rain isn't, at over 9
    // frames a second), and as it ends the second comes 3 ticks on; after
    // the second the next strike is 500 + rand(1500) ticks on with thunder
    // (event_thunder_1, 202) 25 + rand(50) ticks later at -200 + rand(400)
    // each way, when the sound can play (FUN_004b9a00).
    struct Strike { bool flash = false; int thunder = -1, x = 0, y = 0; };
    Strike frame(Rng& roll, bool sound_on) {
        Strike out;
        flashing = false;
        if (!lightning || flash == 0) return out;
        if (--flash == 0) {
            if (!second) { strike_left = 3; second = true; }
            else {
                strike_left = roll(0x5dc) + 500;
                second = false;
                if (!heard) heard = true;
                else if (const int delay = roll(0x32) + 25; sound_on) { out.thunder = delay; out.x = roll(400) - 200; out.y = roll(400) - 200; }
            }
        }
        out.flash = flashing = true;
        return out;
    }
    // Snow or not (FUN_004726f0; `act` 0-based): in Act 5, rand(100) at
    // least the level's bar (25 in 109..112, else 0) snows, `forced` always
    // (a game joined, the player reassigned: S->C 0x15's flag, 1 in every
    // log but two); 120 / 121 hold it full at once. Elsewhere no snow. A
    // change between rain and snow clears the drops.
    void decide(Rng& roll, int level, int act, bool forced) {
        winds(roll);
        bool snows = false, hold = false;
        if (act == 4) {
            int bar = 0;
            if (level >= 109 && level <= 112) { big = false; bar = 25; }
            else if (level == 117) big = true;
            else if (level == 120 || level == 121) { big = true; hold = true; }
            snows = roll(100) >= bar || forced;
        }
        held = snows && hold;
        if (held) enter(roll, 2, level, act);
        if (snows) lightning = false;                        // FUN_00472590
        if (snow != snows || (snows && forced)) drops.clear();
        snow = snows;
    }
    // A state begun (FUN_00473d00): held, only full.
    void enter(Rng& roll, int next, int level, int act) {
        if (held && next != 2) return;
        state = next;
        if (next == 0) density = 0;
        if (next == 1) { decide(roll, level, act, false); target = 0x20 + roll(0xe0); }
        if (next == 2) full(roll, level);
        if (next == 3) lightning = false;
    }
    // Full (FUN_00472400): Act 5's snow levels their own density and wind;
    // elsewhere the target, and a draw on the seed while it rains.
    void full(Rng& roll, int level) {
        if (level >= 109 && level <= 112) { wind_low = 0x2a; wind_span = 0xaa; target = density = 0x20 + roll(0x38); return; }
        if (level == 117) { wind_low = 0xaa; wind_span = 0x38; target = density = 0x20 + roll(0x70); return; }
        if (level == 120 || level == 121) { wind_low = wind_span = wind = wind_to = 0x1c; target = density = 0x100; return; }
        density = target;
        lightning = false;
        if (!snow) roll.next();
    }
    // A game joined in `level` (FUN_00472890): full's base, and snow or not.
    void join(Rng& roll, int level, int act) {
        full_base = act == 2 || act == 4 ? 9000 : 3000;
        decide(roll, level, act, true);
    }
    // The player reassigned (S->C 0x15, FUN_004654c0 -> FUN_00472c20): the
    // drops and splashes go, and snow or not again.
    void reassign(Rng& roll, int level, int act) {
        drops.clear();
        splashes.clear();
        decide(roll, level, act, true);
    }

    // One client frame (FUN_00473f50): the drops fall, the splashes play,
    // the cycle moves on, drops fill up to the density and the wind turns.
    // (sx, sy): how far the view scrolled this frame, pixels. A flake goes
    // at |cos wind| (at least 0.25) of its speed and sways
    // 2 sin(frame x 512 / 25 + its phase) across.
    void tick(Rng& roll, int width, int height, int shift_x, int shift_y, int day_type, int level = 0, int act = 0) {
        const float pace = snow ? std::max(std::abs(cos512(wind)), 0.25f) : volume() * 0.15f + 0.85f;
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
            if (snow && !raindrop.landed) dx += int(sin512(int((unsigned(sway) << 9) / 25u) + raindrop.phase) * 2.f);
            raindrop.x += dx - shift_x;
            if (raindrop.x >= width) raindrop.x -= width;
            if (raindrop.x < 0) raindrop.x += width;
        }
        ++sway;
        std::erase_if(drops, [](const Drop& raindrop) { return raindrop.speed < 0; });
        for (auto& splash : splashes) { splash.x -= shift_x; splash.y -= shift_y; if (--splash.wait < 1) { ++splash.frame; splash.wait = 2; } }   // FUN_00472c80
        std::erase_if(splashes, [&](const Splash& splash) { return splash.frame >= splash_frames[std::size_t(splash.kind)]; });

        if (!held || state == 2) {                 // FUN_00473e50
            int shown = state;
            if (left == 0) {
                shown = (state + 1) % 4;
                length = left = (shown == 2 ? full_base : kBase[std::size_t(shown)]) + roll(kSpread[std::size_t(shown)]);
                enter(roll, shown, level, act);
            }
            --left;
            if (shown == 1) density = int(std::int64_t(length - left) * target / length);
            if (shown == 3) density = target * left / length;
        } else {
            state = 2;
            full(roll, level);
        }

        while (int(drops.size()) < density) drops.push_back(drop(roll, width, height, day_type));   // FUN_004737b0
        const int step = snow ? 1 : 2;
        if (wind_to < wind) wind = std::max(wind - step, wind_to);
        else if (wind < wind_to) wind = std::min(wind + step, wind_to);
        if (--wind_left == 0) {
            if (!snow) { wind_left = roll(0x177) + 0x7d; wind_to = roll(0x47) + 0x5c; }
            else { wind_left = roll(0x36b) + 0xfa; wind_to = wind_low + roll(wind_span); }
        }
        if (lightning && --strike_left == 0) flash = 1;
    }

    // A floor pass begun (FUN_004de730): splashes when 3 client frames have
    // gone since the last such pass (at volume x 1000 in 1000), bubbles in a
    // Mud level when 25 have.
    void floors(bool mud) {
        threshold = int(double(volume()) * 1000.0);
        splash_pass = bubble_pass = false;
        if (splash_at + 3 < clock) { splash_at = clock; splash_pass = threshold != 0; }
        if (mud && bubble_at + 25 < clock) { bubble_at = clock; bubble_pass = true; }
    }
    // A floor tile whose DT1 material flags have 2 drawn with its top
    // corner at (x, y) (FUN_004de410): rand(1000), under the threshold a
    // splash in it (FUN_00472da0), under 100 a bubble (FUN_00472ec0, 30
    // higher). game.exe bug (bugs.md #10): the spot runs from the top corner
    // rightward by the row's width, so they sit half a tile right.
    void floor(Rng& roll, int x, int y) {
        const int chance = roll(1000);
        if (splash_pass && chance < threshold) {
            const int row = roll(80), half = row < 40 ? 40 - row : row - 40;
            Splash splash;
            splash.x = roll((80 - 2 * half) * 2) + x;
            splash.y = row + y;
            splash.kind = int(roll.next() & 1) + 2;
            splashes.push_back(splash);
        }
        if (bubble_pass && chance < 100) {
            const int row = roll(80), half = row < 40 ? 40 - row : row - 40;
            Splash bubble;
            bubble.x = roll((80 - 2 * half) * 2) + x;
            bubble.y = row - 30 + y;
            bubbles.push_back(bubble);
        }
    }
    // A Mud level's frame (FUN_00472d10): the bubbles scroll and play out.
    void tick_bubbles(int shift_x, int shift_y) {
        for (auto& bubble : bubbles) { bubble.x -= shift_x; bubble.y -= shift_y; if (--bubble.wait < 1) { ++bubble.frame; bubble.wait = 2; } }
        std::erase_if(bubbles, [&](const Splash& bubble) { return bubble.frame >= bubble_frames[std::size_t(bubble.kind)]; });
    }
};

// The drops' colours by the time of day when they fell (FUN_00472890,
// made at the join as the palette colours nearest these, FUN_004fb180):
// tone 0..11. A flake's grey (0x7a898c): 120 + 80 x its index / 12, the
// index its shape x 12 / 8, halved at dusk and dawn, a quarter at night
// (FUN_00472fb0, the software modes).
inline std::array<std::uint8_t, 3> rain_rgb(int kind, int tone) {
    if (kind == 0) { const int shade = tone * 80 / 12; return { std::uint8_t(98 - shade), std::uint8_t(123 - shade), std::uint8_t(98 - shade) }; }
    if (kind == 2) { const int shade = tone * 2; return { std::uint8_t(25 - shade), std::uint8_t(30 - shade), std::uint8_t(25 - shade) }; }
    const int shade = tone * 40 / 12;
    return { std::uint8_t(45 - shade), std::uint8_t(55 - shade), std::uint8_t(45 - shade) };
}
inline int flake_tone(int kind, int size) {
    const int index = size * 12 / 8;
    return kind == 0 ? index : kind == 2 ? index / 4 : index / 2;
}
inline std::array<std::uint8_t, 3> snow_rgb(int tone) {
    const auto grey = std::uint8_t(120 + tone * 80 / 12);
    return { grey, grey, grey };
}
// A flake's lines (0x6d6e78, FUN_00473470): shape n draws n and n + 1,
// offsets from it (x1, y1, x2, y2).
inline constexpr std::array<std::array<int, 4>, 9> kFlake{ { { 0, 0, 0, 1 }, { -1, -1, 0, 0 }, { 0, -1, 0, 1 }, { 1, 0, -1, 1 }, { 1, 0, 0, 1 },
                                                             { 2, -1, -1, 1 }, { 1, 1, -2, -1 }, { 2, 1, -2, -1 }, { 2, -1, -2, 2 } } };

}  // namespace d2d::rules
