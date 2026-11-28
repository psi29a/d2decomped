// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Whether a press or held frame sends its skill: rules::press_goes against game.exe's
// FUN_00464600 run by its own code with FUN_004645b0 (tools/emu/press_gate.py writes the
// cases): no item in hand, a player, and by the mode; in SQ from its skill's seqinput frame.
#include <click_spot.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::rules;

int main() try {
    struct Case { int hand, kind, cls, mode, anim, seqinput, frame, goes; };
    constexpr Case cases[]{
#include "press_gate_cases.inc"
    };
    int differ = 0;
    for (const auto& sample : cases) {
        const PressIn press{ .hand_item = sample.hand != 0, .player = sample.kind == 0, .mode = sample.mode, .char_class = sample.cls,
                             .sq_skill = sample.anim == 0x12, .seqinput = sample.seqinput, .frame = sample.frame };
        if (press_goes(press) != (sample.goes != 0)) {
            std::printf("differ: hand %d type %d class %d mode %#x anim %#x seqinput %d frame %d: game.exe %d\n", sample.hand, sample.kind,
                        sample.cls, sample.mode, sample.anim, sample.seqinput, sample.frame, sample.goes);
            ++differ;
        }
    }
    // d2d's -1 (no mode: stand or walk) is NU's.
    assert(press_goes(PressIn{ .mode = -1 }));
    std::printf("%zu cases, %d differ\n", sizeof(cases) / sizeof(cases[0]), differ);
    assert(differ == 0);
    return differ == 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_press_gate: %s\n", error.what());
    return 1;
}
