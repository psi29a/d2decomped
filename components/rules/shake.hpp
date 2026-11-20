// SPDX-License-Identifier: GPL-3.0-or-later
// Client screen shake: game.exe 1.14d FUN_00476a80/00476d40/004769d0.
#pragma once

#include "rules.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace d2d::rules {

struct ScreenShake {
    std::uint32_t born = 0, ramp = 0, hold = 0, fade = 0;
    int strength = 0;
    std::pair<int, int> offset{};

    void start(int power, std::uint32_t rise, std::uint32_t sustain, std::uint32_t fall, std::uint32_t now) {
        if (!sustain) return;   // FUN_00476a80 ignores a zero hold
        born = now; ramp = rise; hold = sustain; fade = fall; strength = std::max(power, 0);
    }
    [[nodiscard]] int envelope(std::uint32_t now) const {
        const std::uint64_t elapsed = std::uint32_t(now - born);
        const std::uint64_t peak = std::uint64_t(ramp) + hold, end = peak + fade;
        if (elapsed > end) return 0;
        if (elapsed < ramp) return int(std::uint64_t(strength) * elapsed / ramp);
        if (elapsed < peak) return strength;
        return fade ? int(std::uint64_t(strength) * (end - elapsed) / fade) : 0;
    }
    // One rendered frame. The client seed is independent of gameplay rolls.
    int step(std::uint32_t now, Rng& rng) {
        const int power = envelope(now);
        if (std::uint64_t(std::uint32_t(now - born)) > std::uint64_t(ramp) + hold + fade) strength = 0;
        offset = {};
        if (power > 0) offset = { int(rng.next() % (std::uint32_t(power) * 2)) - power,
                                  int(rng.next() % (std::uint32_t(power) * 2)) - power };
        return power;
    }
};

// FUN_004769d0: x255 / 20 (not /10), capped; move six volume units
// per rendered frame, including the fade to silence after the shake ends.
int quake_volume(int current, int strength);

// Client-only parts of the quest control missiles (29, 37). Process crossed
// frames once even when rendering skips a game tick. Time starts on receipt,
// as the original client starts its control missile, not on the host's clock.
struct QuestQuake {
    enum Kind { andariel, diablo } kind = andariel;
    std::uint32_t born = 0;
    bool taunted = false, shaken = false;

    template<class Taunt> bool step(std::uint32_t now, ScreenShake& shake, Taunt&& taunt) {
        const auto elapsed = std::uint32_t(now - born);
        // Diablo tests remaining frames (FUN_0064a380), not elapsed.
        if (kind == diablo && !taunted && elapsed >= (300 - 50) * 40) { taunted = true; taunt(); }
        const auto delay = std::uint32_t(kind == andariel ? 90 : 150) * 40;
        if (!shaken && elapsed >= delay) {
            shaken = true;
            // FUN_004d5310: 7 seconds: 2 rise, 3 hold, 2 fall;
            // FUN_004d6540: strength 25, hold 4000, no ramp or fade.
            if (kind == andariel) shake.start(8, 2000, 3000, 2000, born + delay);
            else shake.start(25, 0, 4000, 0, born + delay);
        }
        return shaken && (kind == andariel || taunted);
    }
};

}  // namespace d2d::rules
