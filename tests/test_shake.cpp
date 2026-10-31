// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
#include <rules.hpp>
#include <shake.hpp>

#include <cassert>
#include <cstdint>

int main() {
    struct Case { int strength; std::uint32_t ramp, hold, fade, elapsed; int envelope, volume; };
    constexpr Case cases[]{
#include "shake_cases.inc"
    };
    d2d::rules::ScreenShake shake;
    int volume = 0, previous = -1;
    for (const auto& sample : cases) {
        if (previous != sample.strength) {
            shake.start(sample.strength, sample.ramp, sample.hold, sample.fade, 0xfffffff0u);
            volume = 0; previous = sample.strength;
        }
        assert(shake.envelope(0xfffffff0u + sample.elapsed) == sample.envelope);
        volume = d2d::rules::quake_volume(volume, sample.envelope);
        assert(volume == sample.volume);
    }
    struct ControlCase { int kind, frame, strength; std::uint32_t ramp, hold, fade; int taunt; };
    constexpr ControlCase controls[]{
#include "quake_control_cases.inc"
    };
    d2d::rules::QuestQuake sequence;
    int previous_kind = -1;
    for (const auto& sample : controls) {
        if (sample.kind != previous_kind) {
            sequence = { d2d::rules::QuestQuake::Kind(sample.kind), 0 };
            previous_kind = sample.kind;
        }
        shake = {};
        int said = 0;
        sequence.step(std::uint32_t(sample.frame * 40), shake, [&] { ++said; });
        assert(said == sample.taunt);
        assert(shake.strength == sample.strength && shake.ramp == sample.ramp && shake.hold == sample.hold && shake.fade == sample.fade);
    }
    shake.start(8, 2000, 3000, 2000, 0);
    shake.start(99, 0, 0, 0, 10);   // no hold: original leaves the current shake alone
    assert(shake.envelope(4000) == 8);
    d2d::rules::Rng rng{ 1 };
    for (int frame = 0; frame < 300; ++frame) {
        const int power = shake.step(std::uint32_t(frame * 40), rng);
        assert(power == 0 ? shake.offset.first == 0 && shake.offset.second == 0
                         : shake.offset.first >= -power && shake.offset.first < power && shake.offset.second >= -power && shake.offset.second < power);
    }
    int taunts = 0;
    d2d::rules::QuestQuake control{ d2d::rules::QuestQuake::diablo, 100 };
    const auto taunt = [&] { ++taunts; };
    assert(!control.step(6099, shake, taunt) && taunts == 0);
    assert(!control.step(6140, shake, taunt) && taunts == 0 && shake.envelope(6140) == 25);
    assert(!control.step(6140, shake, taunt) && taunts == 0);
    assert(!control.step(10099, shake, taunt) && taunts == 0);
    assert(control.step(10140, shake, taunt) && taunts == 1);
    control.step(10140, shake, taunt);
    assert(taunts == 1 && shake.envelope(10140) == 0);
    control = { d2d::rules::QuestQuake::andariel, 100 };
    assert(!control.step(3699, shake, taunt));
    assert(control.step(3740, shake, taunt) && taunts == 1 && shake.envelope(5700) == 8);
    shake = {};
    assert(shake.envelope(5700) == 0);
}
