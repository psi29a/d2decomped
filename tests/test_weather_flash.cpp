// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The lightning flash's state machine (Rain::frame, weather.hpp) vs
// game.exe's FUN_00473910 (tools/emu/weather_flash.py): the first flash's
// 3-tick second, the next strike's delay, and the thunder sound's delay
// and position once a strike's already been heard.
#include <rules.hpp>
#include <weather.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iterator>

using namespace d2d::rules;

// Replays each oracle case through Rain::frame and checks its state and thunder against game.exe.
int main() try {
    struct Case {
        int second, heard, sound_ok;
        std::uint32_t seed_low, seed_high;
        int strike_left, second_after, heard_after, thunder, thunder_x, thunder_y;
    };
    constexpr Case kOracle[] = {
#include "weather_flash_cases.inc"
    };
    for (const auto& oracle : kOracle) {
        Rain rain;
        rain.lightning = true;
        rain.flash = 1;             // this tick's decrement always reaches 0
        rain.second = oracle.second != 0;
        rain.heard = oracle.heard != 0;
        Rng rng;
        rng.low = oracle.seed_low;
        rng.high = oracle.seed_high;
        const auto strike = rain.frame(rng, oracle.sound_ok != 0);
        // The oracle's thunder / x / y are what SOUND_PlayEnvironmentSound was given when
        // SOUND_PlaySound answered a channel (sound_ok); the delay is rolled either way (the seed
        // advances the same), but d2d's sound_on gates reporting it, same as a missing channel does.
        const int want_thunder = oracle.sound_ok ? oracle.thunder : -1;
        const int want_x = oracle.sound_ok ? oracle.thunder_x : 0, want_y = oracle.sound_ok ? oracle.thunder_y : 0;
        if (rain.strike_left != oracle.strike_left || int(rain.second) != oracle.second_after || int(rain.heard) != oracle.heard_after
            || strike.thunder != want_thunder || strike.x != want_x || strike.y != want_y)
            std::printf("second %d heard %d sound_ok %d seed %u/%u: got strike_left %d second %d heard %d thunder %d (%d, %d) "
                        "want strike_left %d second %d heard %d thunder %d (%d, %d)\n",
                        oracle.second, oracle.heard, oracle.sound_ok, oracle.seed_low, oracle.seed_high, rain.strike_left, int(rain.second),
                        int(rain.heard), strike.thunder, strike.x, strike.y, oracle.strike_left, oracle.second_after, oracle.heard_after,
                        want_thunder, want_x, want_y);
        std::fflush(stdout);
        assert(rain.strike_left == oracle.strike_left);
        assert(int(rain.second) == oracle.second_after);
        assert(int(rain.heard) == oracle.heard_after);
        assert(strike.thunder == want_thunder);
        assert(strike.x == want_x && strike.y == want_y);
        assert(strike.flash);   // a flash due is always drawn once entered (lightning set, flash != 0)
    }
    std::printf("weather flash: %zu cases match game.exe\n", std::size(kOracle));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "weather flash: %s\n", error.what());
    return 1;
}
