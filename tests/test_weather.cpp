// SPDX-License-Identifier: GPL-3.0-or-later
// The rain's cycle and drops against game.exe's numbers.
#include <rules.hpp>
#include <weather.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>

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
    rain.clock = 4;
    rain.floors(false);                                  // a splash pass: 3 frames on, volume x 1000 in 1000
    assert(rain.splash_pass && !rain.bubble_pass && rain.threshold == 3906);
    for (int k = 0; k < 20; ++k) rain.floor(rng, 100, 100);
    assert(rain.splashes.size() == 20);
    for (const auto& splash : rain.splashes) assert(splash.x >= 100 && splash.x < 260 && splash.y >= 100 && splash.y < 180 && (splash.kind == 2 || splash.kind == 3));
    rain.density = 0;
    for (int tick = 0; tick < 13; ++tick) rain.tick(rng, 800, 600, 0, 0, 0);
    assert(rain.splashes.empty());
    // The next pass in the same frame takes none; in a Mud level a bubble
    // pass comes 25 frames on, a roll under 100 in 1000 bubbling.
    rain.floors(true);
    assert(!rain.splash_pass && !rain.bubble_pass);
    rain.clock = 26;
    rain.floors(true);
    assert(rain.bubble_pass && !rain.splash_pass);
    rain.bubble_frames = { 1, 1, 8, 1 };
    for (int k = 0; k < 400; ++k) rain.floor(rng, 100, 100);
    assert(rain.splashes.empty() && !rain.bubbles.empty() && rain.bubbles.size() < 80);
    for (const auto& bubble : rain.bubbles) assert(bubble.kind == 2 && bubble.y >= 70 && bubble.y < 150);
    for (int tick = 0; tick < 17; ++tick) rain.tick_bubbles(0, 0);
    assert(rain.bubbles.empty());

    // Act 5's snow (FUN_004726f0 / FUN_00472400): joined in the Ancients'
    // Way (120) it snows held full (256 flakes at first), a size bigger, the wind
    // 28..55 easing a step a tick; it stays full.
    Rain snow;
    Rng snow_rng(777);
    snow.join(snow_rng, 120, 4);
    assert(snow.snow && snow.big && snow.held && snow.state == 2 && snow.density == 256 && snow.wind == 0x1c);
    for (int tick = 0; tick < 5000; ++tick) {
        const int was = snow.wind;
        snow.tick(snow_rng, 800, 600, 0, 0, 0, 120, 4);
        assert(snow.state == 2 && std::abs(snow.wind - was) <= 1 && snow.wind >= 0x1c && snow.wind < 0x38);
    }
    // The first tick's countdown runs out: the held cycle refuses falling,
    // but falling's density is taken once (FUN_00473e50): just under 256.
    assert(int(snow.drops.size()) == snow.density && snow.density >= 250 && snow.density < 256);
    for (const auto& flake : snow.drops) assert(flake.size >= 1 && flake.size <= 7 && (flake.landed || (flake.speed >= 8 && flake.speed <= 36)));
    // The Frigid Highlands (111) snow when joined, rain once the player's
    // reassigned out of Act 5; a change clears the drops.
    Rain highlands;
    highlands.join(snow_rng, 111, 4);
    assert(highlands.snow && !highlands.big && !highlands.held);
    highlands.drops.resize(3);
    highlands.reassign(snow_rng, 1, 0);
    assert(!highlands.snow && highlands.drops.empty());
    // A flake's grey: its shape's index, halved at dusk, a quarter at night.
    assert(flake_tone(0, 7) == 10 && flake_tone(1, 7) == 5 && flake_tone(2, 7) == 2 && snow_rgb(0)[0] == 120 && snow_rgb(11)[0] == 193);
    std::puts("test_weather: ok");
}
