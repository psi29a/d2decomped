// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of weather.hpp's functions, with the research behind each.
#include "weather.hpp"

#include "rules.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace d2d::rules {

// The drops' colours by the time of day when they fell (FUN_00472890,
// made at the join as the palette colours nearest these, FUN_004fb180):
// tone 0..11. A flake's grey (0x7a898c): 120 + 80 x its index / 12, the
// index its shape x 12 / 8, halved at dusk and dawn, a quarter at night
// (FUN_00472fb0, the software modes).
std::array<std::uint8_t, 3> rain_rgb(int kind, int tone) {
    if (kind == 0) { const int shade = tone * 80 / 12; return { std::uint8_t(98 - shade), std::uint8_t(123 - shade), std::uint8_t(98 - shade) }; }
    if (kind == 2) { const int shade = tone * 2; return { std::uint8_t(25 - shade), std::uint8_t(30 - shade), std::uint8_t(25 - shade) }; }
    const int shade = tone * 40 / 12;
    return { std::uint8_t(45 - shade), std::uint8_t(55 - shade), std::uint8_t(45 - shade) };
}

int flake_tone(int kind, int size) {
    const int index = size * 12 / 8;
    return kind == 0 ? index : kind == 2 ? index / 4 : index / 2;
}

std::array<std::uint8_t, 3> snow_rgb(int tone) {
    const auto grey = std::uint8_t(120 + tone * 80 / 12);
    return { grey, grey, grey };
}



// A Thunder Storm strike (FUN_00472c50, ECX 0): lightning if there's none.
auto Rain::set_off() -> void {
    if (lightning) return;
    lightning = true; flash = 1; second = false; heard = false;
}

// FUN_00473910: this client tick's flash, and past it the next strike's wait and, once a
// strike's already been heard, the thunder's delay and position (y rolled before x).
auto Rain::frame(Rng& roll, bool sound_on) -> Strike {
    Strike out;
    flashing = false;
    if (!lightning || flash == 0) return out;
    if (--flash == 0) {
        if (!second) { strike_left = 3; second = true; }
        else {
            strike_left = roll(0x5dc) + 500;
            second = false;
            if (!heard) heard = true;
            // The two rolls land y then x (FUN_00473910 0x473a02..0x473a2b: the first roll is
            // pushed and read back as SOUND_PlayEnvironmentSound's nPosY, the second is its nPosX).
            else if (const int delay = roll(0x32) + 25; sound_on) { out.thunder = delay; out.y = roll(400) - 200; out.x = roll(400) - 200; }
        }
    }
    out.flash = flashing = true;
    return out;
}

// A game joined in `level` (FUN_00472890): full's base, and snow or not.
auto Rain::join(Rng& roll, int level, int act) -> void {
    full_base = act == 2 || act == 4 ? 9000 : 3000;
    decide(roll, level, act, true);
}

// The player reassigned (S->C 0x15, FUN_004654c0 -> FUN_00472c20): the
// drops and splashes go, and snow or not again.
auto Rain::reassign(Rng& roll, int level, int act) -> void {
    drops.clear();
    splashes.clear();
    decide(roll, level, act, true);
}

// A floor pass begun (FUN_004de730): splashes when 3 client frames have
// gone since the last such pass (at volume x 1000 in 1000), bubbles in a
// Mud level when 25 have.
auto Rain::floors(bool mud) -> void {
    threshold = int(double(volume()) * 1000.0);
    splash_pass = bubble_pass = false;
    if (splash_at + 3 < clock) { splash_at = clock; splash_pass = threshold != 0; }
    if (mud && bubble_at + 25 < clock) { bubble_at = clock; bubble_pass = true; }
}

auto Rain::floor(Rng& roll, int x, int y) -> void {
    const int chance = roll(1000);
    if (splash_pass && chance < threshold && splashes.size() < kSplashPool) {
        const int row = roll(80), half = row < 40 ? 40 - row : row - 40, width = (80 - 2 * half) * 2;
        Splash splash;
        splash.x = roll(width) + x - (centre ? width / 2 : 0);
        splash.y = row + y;
        splash.kind = int(roll.next() & 1) + 2;
        splashes.push_back(splash);
    }
    if (bubble_pass && chance < 100 && bubbles.size() < kBubblePool) {
        const int row = roll(80), half = row < 40 ? 40 - row : row - 40, width = (80 - 2 * half) * 2;
        Splash bubble;
        bubble.x = roll(width) + x - (centre ? width / 2 : 0);
        bubble.y = row - 30 + y;
        bubbles.push_back(bubble);
    }
}

// A Mud level's frame (FUN_00472d10): the bubbles scroll and play out.
auto Rain::tick_bubbles(int shift_x, int shift_y) -> void {
    for (auto& bubble : bubbles) { bubble.x -= shift_x; bubble.y -= shift_y; if (--bubble.wait < 1) { ++bubble.frame; bubble.wait = 2; } }
    std::erase_if(bubbles, [&](const Splash& bubble) { return bubble.frame >= bubble_frames[std::size_t(bubble.kind)]; });
}

}
