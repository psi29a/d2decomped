// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of sound_mix.hpp's functions; their notes are in the header.
#include "sound_mix.hpp"

#include <algorithm>
#include <cmath>

namespace d2d::rules {

SoundMix sound_mix_pixels(int volume, int falloff, float screen_x, float screen_y2) {
    const auto [near_px, far_px] = falloff_range(falloff);
    const float clamped_x = std::clamp(screen_x, -2000.f, 2000.f), clamped_y = std::clamp(screen_y2, -2000.f, 2000.f);
    SoundMix mix;
    mix.audible = clamped_x * clamped_x + clamped_y * clamped_y <= far_px * far_px;
    if (!mix.audible) return mix;
    int level = 255;
    if (const float distance = std::sqrt(screen_x * screen_x + screen_y2 * screen_y2); near_px < distance)
        // max(.., 1): the same value for every range that gets here (far - near is 340 or more), and
        // no zero divisor for MSVC to fold from Falloff 4's near == far (C4723, an error on ARM64).
        level = far_px > near_px ? std::max(0, int((far_px - distance) * 255.f / std::max(far_px - near_px, 1.f))) : 0;
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

SoundMix sound_mix(int volume, int falloff, float dx, float dy) {
    const auto [screen_x, screen_y2] = screen_offset(dx, dy);
    return sound_mix_pixels(volume, falloff, screen_x, screen_y2);
}

}
