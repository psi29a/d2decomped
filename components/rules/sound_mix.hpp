// SPDX-License-Identifier: GPL-3.0-or-later
// How loud, and where, a world sound plays for the listener: game.exe's
// 2D mix (FUN_004dfc20 with FUN_00516830; docs/research/re/unit-sounds.md
// "Distance"). Checked against game.exe in tools/emu/sound_falloff.py.
#pragma once

#include <algorithm>
#include <cmath>

namespace d2d::rules {

// Sounds.txt Falloff 0..4: full volume within `near_px`, silent (stopped)
// past `far_px` (FUN_004825b0, FUN_00482610), in client pixels.
struct Falloff { float near_px, far_px; };
constexpr Falloff falloff_range(int falloff) {
    switch (falloff) {
    case 2: return { 200.f, 1000.f };
    case 3: return { 400.f, 1500.f };
    case 4: return { 2000.f, 2000.f };
    case 0: return { 60.f, 400.f };
    default: return { 60.f, 700.f };
    }
}

struct SoundMix {
    bool audible = false;   // within far_px: game.exe stops it past that
    int volume = 0;         // 0..255, the amplitude (FUN_005165f0: 20 log10(v / 255) dB)
    int pan = 128;          // 0 left .. 128 middle .. 255 right
};

// The sound `screen_x`, `screen_y2` / 2 client pixels from the listener,
// at Sounds.txt `volume` (0..255).
inline SoundMix sound_mix_pixels(int volume, int falloff, float screen_x, float screen_y2) {
    const auto [near_px, far_px] = falloff_range(falloff);
    const float clamped_x = std::clamp(screen_x, -2000.f, 2000.f), clamped_y = std::clamp(screen_y2, -2000.f, 2000.f);
    SoundMix mix;
    mix.audible = clamped_x * clamped_x + clamped_y * clamped_y <= far_px * far_px;
    if (!mix.audible) return mix;
    int level = 255;
    if (const float distance = std::sqrt(screen_x * screen_x + screen_y2 * screen_y2); near_px < distance)
        level = far_px > near_px ? std::max(0, int((far_px - distance) * 255.f / (far_px - near_px))) : 0;
    level = level * volume / 255;
    // FUN_00516830: 6 dB down a doubling of the distance from a point 640 px
    // above the floor (capped at 100 x 320 px); pan by x, 400 px to the side.
    const float x = screen_x / 320.f, y = screen_y2 / 320.f, height = 2.f;
    const float reach = std::min(std::sqrt(x * x + y * y + height * height), 100.f);
    const float decibels = reach < 2.f ? 0.f : std::log(reach * 0.5f) / 0.6931471805599453f * 6.f / 20.f;
    const int distance_level = std::clamp(int(255.0 / std::pow(10.0, double(decibels))), 0, 255);
    mix.pan = std::clamp(int(double(x) * 127.0 / 1.25 + 128.0), 0, 255);
    mix.volume = level * distance_level / 255;
    return mix;
}

// The two speakers' amplitudes, 0..1: DirectSound's volume (`FUN_005157b0`)
// times its pan (`FUN_00515890`): past the middle the far speaker goes down
// to (255 - pan) / 127, or pan / 127 to the left, by 2000 log10 hundredths
// of a dB (`FUN_005165f0`); the near one stays whole.
struct Speakers { float left, right; };
constexpr Speakers speakers(const SoundMix& mix) {
    const float volume = float(mix.volume) / 255.f;
    const float left = mix.pan > 128 ? float(255 - mix.pan) / 127.f : 1.f;
    const float right = mix.pan < 128 ? float(mix.pan) / 127.f : 1.f;
    return { volume * left, volume * right };
}
static_assert(speakers({ true, 255, 128 }).left == 1.f && speakers({ true, 255, 128 }).right == 1.f);
static_assert(speakers({ true, 255, 255 }).left == 0.f && speakers({ true, 255, 0 }).right == 0.f);

// The same `dx`, `dy` cells away (d2d's cells: 5 subtiles, a subtile 32 x 16
// client pixels).
inline SoundMix sound_mix(int volume, int falloff, float dx, float dy) {
    return sound_mix_pixels(volume, falloff, (dx - dy) * 80.f, (dx + dy) * 80.f);
}

}  // namespace d2d::rules
