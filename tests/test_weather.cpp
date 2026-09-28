// The rain's cycle and drops against game.exe's numbers.
#include <weather.hpp>

#include <cassert>
#include <cstdio>

using namespace d2d::rules;

int main() {
    Rain rain;
    rain.splash_frames = { 1, 1, 6, 6 };
    Rng r(12345);
    // The first tick starts it rising: 250..499 ticks toward 32..255 drops.
    rain.tick(r, 800, 600, 0, 0, 0);
    assert(rain.state == 1 && rain.length >= 250 && rain.length < 500);
    assert(rain.target >= 32 && rain.target < 256 && rain.wind >= 0x5c && rain.wind < 0x5c + 0x47);
    int most = 0, full_at = -1;
    for (int t = 1; t < 30000 && rain.state != 3; ++t) {
        rain.tick(r, 800, 600, 0, 0, 0);
        most = std::max(most, int(rain.drops.size()));
        if (rain.state == 2 && full_at < 0) full_at = t;
        for (const auto& d : rain.drops) {
            assert(d.x >= 0 && d.x < 800 && d.bottom >= 40 && d.bottom < 553);
            assert(d.len >= 4 && d.len <= 12 && (d.landed || (d.speed >= 15 && d.speed <= 30)));
            assert(d.y <= d.bottom + 40);
        }
    }
    assert(full_at > 0 && full_at < 500 && rain.state == 3 && most == rain.target);
    // Falling, then clear: no drops left once they've landed.
    for (int t = 0; t < 400; ++t) rain.tick(r, 800, 600, 0, 0, 2);
    assert(rain.state == 0 && rain.density == 0 && rain.drops.empty());
    // Splashes land in a tile at most one width right of its top corner, and play out.
    rain.density = 1000;
    for (int k = 0; k < 20; ++k) rain.floor(r, 100, 100);
    assert(rain.splashes.size() == 20);
    for (const auto& s : rain.splashes) assert(s.x >= 100 && s.x < 260 && s.y >= 100 && s.y < 180 && (s.kind == 2 || s.kind == 3));
    rain.density = 0;
    for (int t = 0; t < 13; ++t) rain.tick(r, 800, 600, 0, 0, 0);
    assert(rain.splashes.empty());
    std::puts("test_weather: ok");
}
