// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-sources: apps/d2d/touch_gestures.cpp
// test-includes: apps/d2d
// Holds distinguish taps, can keep a target, and dispatch only when ready.
#include "gamepad_map.hpp"
#include "touch_feedback.hpp"
#include "touch_gestures.hpp"
#include "touch_input.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

int main() try {
    using d2d::client::touch::HoldGesture;
    HoldGesture hold;
    hold.begin(7, 1000);
    assert(!hold.update(1299));
    assert(hold.update(1300));
    assert(hold.update(1301));
    hold.clear();
    assert(!hold.update(2000));
    hold.begin(-2, 2000); // mouse-backed touch uses -2 as its owner
    assert(!hold.update(2299));
    assert(hold.update(2300));
    hold.clear();
    hold.begin(8, UINT32_MAX - 100);
    assert(hold.update(199)); // elapsed time wraps safely
    hold.clear();
    assert(!hold.update(500));

    using namespace d2d::client;
    const auto box = touch::layout_boxes().skills.front();
    const float x = box.x + box.width / 2, y = box.y + box.height / 2;
    const touch::Target spell{ 42, 11, 3.f, 0.f, false, true };
    touch::Input input;
    touch::SkillHold skill;

    input.event(7, touch::Phase::Down, x, y, spell);
    assert(input.owns_skill(7));
    skill.begin(7, 1000, spell.unit, 1.f, 0.f);
    assert(!skill.dispatch(1299, true, 40));
    assert(skill.release_casts());
    input.event(7, touch::Phase::Up, x, y);
    assert(!input.owns_skill(7) && input.casts.size() == 1);
    assert(input.casts.front().unit == spell.unit);
    input.frame();

    input.event(-2, touch::Phase::Down, x, y, spell);
    input.event(8, touch::Phase::Down, x, y, spell);
    assert(input.owns_skill(-2) && !input.owns_skill(8));
    skill.begin(-2, 2000, spell.unit, 1.f, 0.f);
    const pad::Aim aim{ 8.f, 10.f };
    const std::array foes{ pad::Candidate{ 12, 1.f, 0.f }, pad::Candidate{ 11, 7.f, 0.f } };
    skill.resolve(input.preview, 0, 0, foes, aim);
    assert(input.preview.valid && input.preview.unit == 11); // Keep the live target even when another closes in.
    skill.steer(2.f, 1.f);
    skill.resolve(input.preview, 0, 0, foes, aim);
    assert(input.preview.unit == 11); // Dragging does not break an eligible lock.
    assert(skill.dispatch(2300, true, 40));
    assert(skill.sent_to == 11);
    assert(!skill.dispatch(2301, true, 40)); // Rendering frames cannot resend a pending command.
    assert(!skill.dispatch(2340, false, 40)); // Busy or cooling down.
    assert(skill.dispatch(2380, true, 40));
    assert(!skill.release_casts());

    const std::array survivors{ pad::Candidate{ 13, -1.f, 0.f }, pad::Candidate{ 12, 3.f, 0.f } };
    skill.resolve(input.preview, 0, 0, survivors, aim);
    assert(input.preview.valid && input.preview.unit == 12); // Reacquire in the same direction.
    assert(!skill.dispatch(2499, true, 160)); // Remote sends leave time for the view to arrive.
    assert(skill.dispatch(2540, true, 160));
    assert(skill.sent_to == 12);
    skill.resolve(input.preview, 9.f, 8.f, {}, aim);
    assert(input.preview.valid && input.preview.unit == -1 && input.preview.x == 11.f && input.preview.y == 9.f);
    assert(skill.dispatch(2700, true, 160)); // Keep casting along the drag when the last enemy dies.
    skill.steer(-3.f, 0.f);
    skill.resolve(input.preview, 7.f, 4.f, {}, aim);
    assert(input.preview.x == 4.f && input.preview.y == 4.f);
    assert(skill.dispatch(2860, true, 160));
    input.event(8, touch::Phase::Up, x, y);
    assert(input.owns_skill(-2) && input.casts.empty());
    if (!skill.release_casts()) input.consume_release();
    input.event(-2, touch::Phase::Up, x, y);
    assert(!input.owns_skill(-2) && input.casts.empty());
    skill.clear();
    assert(!skill.dispatch(3000, true, 40));

    skill.begin(7, 3000, -1, 1.f, 0.f);
    touch::Target attack{ 0, -1, 0.f, 0.f, true, true };
    skill.resolve(attack, 5.f, 6.f, {}, aim);
    assert(attack.valid && attack.left && attack.unit == -1 && attack.x == 6.f && attack.y == 6.f);
    assert(skill.dispatch(3300, true, 40));
    assert(!touch::beyond_skill_aim(185.f, -125.f));
    assert(touch::beyond_skill_aim(210.f, 0.f));
    skill.cancel();
    assert(!skill.dispatch(3300, true, 40) && !skill.release_casts());
    skill.clear();
    assert(skill.release_casts());

    const auto caster = touch::caster_target(77, 5.f, 6.f);
    input.event(7, touch::Phase::Down, x, y, caster, true);
    input.event(7, touch::Phase::Move, x - 35.f, y - 25.f);
    assert(input.aiming && !input.dragged && input.casts.empty());
    input.event(7, touch::Phase::Up, x - 35.f, y - 25.f);
    assert(input.casts.size() == 1);
    assert(input.casts[0].unit == -1 && !input.casts[0].left && input.casts[0].x == 5.f && input.casts[0].y == 6.f);
    input.frame();

    input.event(7, touch::Phase::Down, x, y, caster, true);
    skill.begin(7, 4000, -1, 0.f, 0.f, true);
    skill.resolve(input.preview, 7.f, 8.f, {}, aim);
    assert(input.preview.valid && input.preview.unit == -1 && input.preview.x == 7.f && input.preview.y == 8.f);
    assert(!skill.dispatch(4299, true, 40));
    assert(skill.dispatch(4300, true, 40)); // The empty field still permits the Nova.
    assert(!skill.dispatch(4301, true, 40));
    skill.resolve(input.preview, 10.f, 11.f, foes, aim);
    assert(input.preview.valid && input.preview.unit == -1 && !input.preview.left);
    assert(input.preview.x == 10.f && input.preview.y == 11.f && !skill.chases);
    assert(skill.dispatch(4340, true, 40));
    if (!skill.release_casts()) input.consume_release();
    input.event(7, touch::Phase::Up, x, y);
    assert(input.casts.empty()); // A hold already dispatched its cast.
    input.clear();
    input.event(7, touch::Phase::Down, x, y, spell, false, false, 5000);
    input.consume_release();
    input.event(7, touch::Phase::Up, x, y, {}, false, false, 5400);
    assert(input.casts.empty());
    assert(input.feedback.sample(0, 5400).kind == touch::FeedbackKind::Accepted);
    input.clear();
    input.event(7, touch::Phase::Down, x, y, spell, false, false, 6000);
    input.consume_release();
    input.event(7, touch::Phase::Up, -1, y, {}, false, false, 6400);
    assert(input.casts.empty());
    assert(input.feedback.sample(0, 6400).kind == touch::FeedbackKind::Cancelled);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_touch_holds: %s\n", error.what());
    return 1;
}
