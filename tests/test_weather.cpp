// SPDX-License-Identifier: GPL-3.0-or-later
// The rain's cycle and drops against game.exe's numbers.
#include <rules.hpp>
#include <weather.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>

using namespace d2d::rules;

int main() {
    Rain rain;
    rain.splash_frames = { 1, 1, 6, 6 };
    Rng rng(12345);
    // The first tick starts it rising: 250..499 ticks toward 32..255 drops.
    rain.tick(rng, 800, 600, 0, 0, 0);
    assert(rain.state == 1 && rain.length >= 250 && rain.length < 500);
    assert(rain.target >= 32 && rain.target < 256 && rain.wind >= 0x5c && rain.wind < 0x5c + 0x47);
    int most = 0, full_at = -1;
    for (int tick = 1; tick < 30000 && rain.state != 3; ++tick) {
        rain.tick(rng, 800, 600, 0, 0, 0);
        most = std::max(most, int(rain.drops.size()));
        if (rain.state == 2 && full_at < 0) full_at = tick;
        for (const auto& drop : rain.drops) {
            assert(drop.x >= 0 && drop.x < 800 && drop.bottom >= 40 && drop.bottom < 553);
            assert(drop.len >= 4 && drop.len <= 12 && (drop.landed || (drop.speed >= 15 && drop.speed <= 30)));
            assert(drop.y <= drop.bottom + 40);
        }
    }
    assert(full_at > 0 && full_at < 500 && rain.state == 3 && most == rain.target);
    // Falling, then clear: no drops left once they've landed.
    for (int tick = 0; tick < 400; ++tick) rain.tick(rng, 800, 600, 0, 0, 2);
    assert(rain.state == 0 && rain.density == 0 && rain.drops.empty());
    // Splashes land in a tile at most one width right of its top corner, and play out.
    rain.density = 1000;
    for (int k = 0; k < 20; ++k) rain.floor(rng, 100, 100);
    assert(rain.splashes.size() == 20);
    for (const auto& splash : rain.splashes) assert(splash.x >= 100 && splash.x < 260 && splash.y >= 100 && splash.y < 180 && (splash.kind == 2 || splash.kind == 3));
    rain.density = 0;
    for (int tick = 0; tick < 13; ++tick) rain.tick(rng, 800, 600, 0, 0, 0);
    assert(rain.splashes.empty());
    std::puts("test_weather: ok");
}
