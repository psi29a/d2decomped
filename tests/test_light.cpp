// SPDX-License-Identifier: GPL-3.0-or-later
// The day's clock and the light grid against game.exe's numbers.
#include <light.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <utility>

using namespace d2d::rules;

int main() {
    Day day;
    assert(day.phase == 2 && day.intensity() == 128);            // sunrise
    day.time = 90 * kDayScale;
    assert(day.intensity() == 255);                            // noon (256 clamped)
    day.time = 270 * kDayScale;
    assert(day.intensity() == 64);                             // midnight

    // A whole day from sunrise: dusk at 160°, night at 200° (two frames
    // a frame), dawn at 320° and 340°, and back at sunrise the frame after.
    Day other_day;
    int frames = 0, phases = 0, last = other_day.phase;
    do {
        other_day.step(); ++frames;
        if (other_day.phase != last) { ++phases; last = other_day.phase; }
    } while (!(other_day.phase == 2 && other_day.time == 0));
    assert(phases == 6);
    // Each phase runs a frame past its end (the clock snaps back); the
    // 340° phase lasts one frame. About 24 minutes at 25 frames a second.
    assert(frames == 20481 + 2561 + 2561 + 7681 + 2561 + 1);
    // Act 3's nights run ten frames a frame: game.exe's day there is 29702
    // frames (FUN_0061bee0 in tools/emu, ESI 2), night 1537.
    Day act3_day;
    frames = 0;
    do { act3_day.step(2); ++frames; } while (!(act3_day.phase == 2 && act3_day.time == 0));
    assert(frames == 29702);
    // Act 4's clock runs 16 frames a frame (FUN_0061bee0, ESI 3): 2726.
    Day act4_day;
    frames = 0;
    do { act4_day.step(3); ++frames; } while (!(act4_day.phase == 2 && act4_day.time == 0));
    assert(frames == 2726);
    // The eclipse from sunrise, as game.exe has it after 10001, 30001 and
    // 60000 frames: Act 1 by 0x744480, Act 4 past 0x744438's starts.
    using Checks = std::array<std::array<int, 2>, 3>;
    for (const auto& [act, checks] : { std::pair{ 1, Checks{ { { 2, 10001 }, { 4, 29999 }, { 3, 21593 } } } },
                                       std::pair{ 3, Checks{ { { 2, 10001 }, { 3, 22320 }, { 2, 13914 } } } } }) {
        Day dark;
        dark.eclipse = true;
        std::size_t check = 0;
        for (int frame = 0; frame < 60000; ++frame) {
            dark.step(act);
            if (frame == 10000 || frame == 30000 || frame == 59999) { assert(dark.phase == checks[check][0] && dark.time == checks[check][1]); ++check; }
        }
    }

    // The player's light (radius 13 subtiles, 255) on a dark grid: full
    // where they stand, falling off linearly, nothing past the radius.
    LightGrid grid;
    grid.reset(100, 100, 0);
    grid.stamp(100 * 8 + 4, 100 * 8 + 4, 13 * 8, 255);
    assert(grid.at(100, 100) > 240);
    assert(grid.at(106, 100) > 100 && grid.at(106, 100) < 160);
    assert(grid.at(114, 100) == 0 && grid.at(100, 87) == 0);
    assert(grid.at(99, 100) == grid.at(101, 100) || std::abs(grid.at(99, 100) - grid.at(101, 100)) < 30);
    // Lights add up, capped; outside the grid the edge.
    grid.stamp(100 * 8 + 4, 100 * 8 + 4, 13 * 8, 255);
    assert(grid.at(100, 100) == 255 && grid.at(-50, 100) == grid.at(76, 100));

    // A wall 4 subtiles east of the light, 7 high: behind it dark, beside
    // it lit, in front of it lit as before.
    LightGrid walled;
    walled.reset(100, 100, 0);
    for (int y = 97; y <= 103; ++y) walled.blocked[std::size_t((y - walled.origin_y) * walled.grid_size + (104 - walled.origin_x))] = 1;
    walled.stamp_shadowed(100 * 8 + 4, 100 * 8 + 4, 13 * 8, 255);
    LightGrid open;
    open.reset(100, 100, 0);
    open.stamp(100 * 8 + 4, 100 * 8 + 4, 13 * 8, 255);
    assert(walled.at(102, 100) == open.at(102, 100) && walled.at(104, 100) == open.at(104, 100));   // up to and on the wall
    assert(walled.at(107, 100) == 0 && open.at(107, 100) > 0);                                 // behind it
    assert(walled.at(100, 108) == open.at(100, 108));                                          // off to the side
    // A bigger grid: a light 40 subtiles off still lands; game.exe's size loses it.
    LightGrid big, game;
    big.reset(100, 100, 0, 120); game.reset(100, 100, 0);
    big.stamp(140 * 8 + 4, 100 * 8 + 4, 18 * 8, 255); game.stamp(140 * 8 + 4, 100 * 8 + 4, 18 * 8, 255);
    assert(big.at(140, 100) > 240 && game.at(140, 100) < 60);   // game.exe's reads its clamped edge
    // The eclipse (S->C 0x53 {5, 0, 1}): 8 a frame down to 32; {2, 0, 0}
    // is sunrise again.
    {
        Day sky;
        sky.set(5, 0, true);
        assert(sky.eclipse && sky.intensity() == sky.sun() - 8);
        for (int i = 0; i < 40; ++i) sky.step();
        assert(sky.intensity() == 32);
        sky.set(2, 0, false);
        assert(!sky.eclipse && sky.intensity() == 128);
    }
    std::puts("test_light: ok");
}
