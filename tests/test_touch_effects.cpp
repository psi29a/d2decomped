// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// test-includes: apps/d2d
// test-sources: apps/d2d/input/touch/effects.cpp apps/d2d/input/touch/gestures.cpp
#include "input/touch/effects.hpp"
#include "input/touch/gestures.hpp"
#include "input/touch/input.hpp"
#include "world.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

int main() try {
    using namespace d2d::client::touch;
    const auto layout = layout_boxes();
    const auto box = effect_box(layout, 0);
    const float x = box.x + 10, y = box.y + 10;
    std::vector<d2d::game::View::Effect> effects{ { 0, 54, 101, 1000, true } };
    EffectGesture gesture;
    const auto tap = [&](std::uint32_t now) {
        assert(gesture.event(1, Phase::Down, x, y, layout, effects, now).consumed);
        assert(gesture.event(1, Phase::Up, x, y, layout, effects, now + 30).consumed);
    };
    assert(effect_page_start(0, 4, 0) == 0 && effect_page_start(0, 0, 10) == 0);
    assert(effects_per_page(10, 4) == 3 && effect_page_start(10, 4, 4) == 0);
    assert(effect_capacity(layout) > 1);
    gesture.event(1, Phase::Down, x, y, layout, effects, 100);
    assert(!gesture.inspecting(599) && gesture.inspecting(600));
    gesture.event(1, Phase::Up, x, y, layout, effects, 700);
    assert(!gesture.modal);
    tap(800); tap(900); assert(gesture.modal);
    const auto confirm = effect_confirm();
    gesture.event(2, Phase::Down, confirm.x + 10, confirm.y + 10, layout, effects, 950);
    assert(!gesture.event(3, Phase::Up, confirm.x + 10, confirm.y + 10, layout, effects, 960).dismiss);
    const auto result = gesture.event(2, Phase::Up, confirm.x + 10, confirm.y + 10, layout, effects, 970);
    assert(result.dismiss && result.dismiss->instance == 101 && !gesture.modal);
    tap(1100); tap(1200); assert(gesture.modal);
    effects[0].instance = 102;
    assert(!gesture.event(2, Phase::Down, confirm.x + 10, confirm.y + 10, layout, effects, 1250).dismiss && !gesture.modal);
    effects[0].dismissible = false; tap(1300); tap(1400); assert(!gesture.modal);
    gesture.cancel();
    effects[0].dismissible = true;
    tap(1500); gesture.event(1, Phase::Down, x, y, layout, effects, 1600);
    gesture.event(1, Phase::Move, 799, 1, layout, effects, 1610);
    gesture.event(1, Phase::Up, x, y, layout, effects, 1620); assert(!gesture.modal);
    tap(1700); tap(1800); assert(gesture.modal);
    gesture.event(2, Phase::Down, confirm.x + 10, confirm.y + 10, layout, effects, 1850);
    gesture.event(2, Phase::Move, 799, 1, layout, effects, 1860);
    assert(!gesture.event(2, Phase::Up, confirm.x + 10, confirm.y + 10, layout, effects, 1870).dismiss);
    gesture.cancel(); assert(!gesture.modal && gesture.finger == -1);
    tap(1900); tap(2000); assert(gesture.modal);
    HoldGesture pickup;
    SkillHold skill;
    Input input;
    pickup.begin(8, 1900);
    // The modal consumes the Get release before field routing sees it.
    assert(gesture.event(8, Phase::Up, 600, 500, layout, effects, 2050).consumed);
    std::int64_t ground_finger = 9;
    cancel_modal_gestures(gesture.modal, input, pickup, skill, ground_finger);
    assert(ground_finger == -1);
    gesture.cancel();
    assert(!pickup.update(2400) && !pickup.activated && pickup.finger == -1);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
