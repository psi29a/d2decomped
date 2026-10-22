// SPDX-License-Identifier: GPL-3.0-or-later
// A world sound's volume and pan by its distance (rules/sound_mix.hpp) vs
// game.exe's own FUN_00516830 (tools/emu/sound_falloff.py: dx, dy, volume, pan).
#include <sound_mix.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
    struct Case { int dx, dy, volume, pan; };
    constexpr Case kOracle[] = {
        { 0, 0, 255, 128 }, { 0, 50, 251, 128 }, { 0, -200, 216, 128 }, { 0, 500, 137, 128 },
        { 100, 0, 251, 159 }, { 100, -200, 214, 159 }, { -100, 50, 249, 96 }, { -100, 500, 137, 96 },
        { 400, 0, 216, 255 }, { 400, -200, 191, 255 }, { -400, 50, 214, 1 }, { -400, 500, 130, 1 },
        { 1000, 0, 137, 255 }, { 1000, 50, 137, 255 }, { 1000, -200, 130, 255 }, { 1000, 500, 105, 255 },
    };
    for (const auto& oracle : kOracle) {
        // Falloff 4 (2000 px near) leaves only FUN_00516830's part.
        const auto mix = d2d::rules::sound_mix_pixels(255, 4, float(oracle.dx), 2.f * float(oracle.dy));
        if (!mix.audible || mix.volume != oracle.volume || mix.pan != oracle.pan)
            std::printf("dx %d dy %d: got %d/%d want %d/%d\n", oracle.dx, oracle.dy, mix.volume, mix.pan, oracle.volume, oracle.pan);
        assert(mix.audible && mix.volume == oracle.volume && mix.pan == oracle.pan);
    }
    // Falloff 0 (a torch): full to 60 px, linear to 400 px, stopped past it.
    assert(d2d::rules::sound_mix_pixels(90, 0, 50.f, 0.f).volume == 90 * 255 / 255 * 254 / 255);
    assert(d2d::rules::sound_mix_pixels(90, 0, 230.f, 0.f).volume == (127 * 90 / 255) * 240 / 255);
    assert(!d2d::rules::sound_mix_pixels(90, 0, 401.f, 0.f).audible);
    assert(!d2d::rules::sound_mix(255, 0, 3.6f, 0.f).audible && d2d::rules::sound_mix(255, 0, 3.5f, 0.f).audible);
    // The speakers: FUN_005165f0 (0, 127, v) is 2000 log10(v / 127) hundredths of a dB
    // (sound_falloff.py: 63.5 -> -602.06, 12.7 -> -2000), so the far one is v / 127.
    const auto right_of_middle = d2d::rules::speakers({ true, 255, 191 });
    assert(right_of_middle.right == 1.f && std::fabs(right_of_middle.left - 64.f / 127.f) < 1e-6f);
    const auto left_quiet = d2d::rules::speakers({ true, 51, 13 });
    assert(left_quiet.left == 51.f / 255.f && std::fabs(left_quiet.right - 51.f / 255.f * 13.f / 127.f) < 1e-6f);
    std::puts("test_sound_mix: ok");
    return 0;
}
