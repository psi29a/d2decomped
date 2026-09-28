// The day's clock and the light grid against game.exe's numbers.
#include <light.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>

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
    std::puts("test_light: ok");
}
