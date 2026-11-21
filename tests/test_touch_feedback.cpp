// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
#include "touch_feedback.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>

int main() {
    using namespace d2d::client::touch;
    FeedbackState feedback;
    assert(feedback.pulse(2, FeedbackKind::Accepted, 100));
    auto sample = feedback.sample(2, 155);
    assert(sample.active && sample.kind == FeedbackKind::Accepted);
    assert(std::abs(sample.phase - .25f) < .001f && std::abs(sample.strength - .75f) < .001f);
    assert(!feedback.pulse(2, FeedbackKind::Rejected, 200)); // shared accept/reject throttle
    assert(feedback.pulse(2, FeedbackKind::Rejected, 280));
    assert(feedback.sample(2, 280).kind == FeedbackKind::Rejected);
    assert(!feedback.sample(2, 580).active);

    assert(feedback.pulse(3, FeedbackKind::Cancelled, 300));
    feedback.press(4, 301);
    assert(feedback.sample(3, 301).active && feedback.sample(4, 900).kind == FeedbackKind::Pressed);
    feedback.release(4);
    assert(!feedback.sample(4, 900).active && feedback.sample(3, 479).active);
    assert(!feedback.sample(3, 480).active);

    // Unsigned subtraction keeps both pulse expiry and throttle valid across wraparound.
    constexpr std::uint32_t near_wrap = UINT32_MAX - 99u;
    assert(feedback.pulse(5, FeedbackKind::Accepted, near_wrap));
    sample = feedback.sample(5, 20u);
    assert(sample.active && std::abs(sample.phase - 120.f / 220.f) < .001f);
    assert(!feedback.pulse(5, FeedbackKind::Rejected, 30u));
    assert(feedback.pulse(5, FeedbackKind::Rejected, 100u));

    // Simultaneous menu press and destination flash stay independent.
    feedback.press(7, 700);
    assert(feedback.pulse(0, FeedbackKind::Accepted, 700));
    assert(feedback.sample(7, 800).kind == FeedbackKind::Pressed);
    assert(feedback.sample(0, 800).kind == FeedbackKind::Accepted);
    feedback.release(7);
    assert(!feedback.sample(7, 800).active && feedback.sample(0, 800).active);
    assert(!feedback.pulse(0, FeedbackKind::Rejected, 800));
    assert(feedback.pulse(1, FeedbackKind::Rejected, 800));

    feedback.reset();
    assert(!feedback.sample(2, 400).active && !feedback.sample(5, 400).active);
    assert(feedback.pulse(2, FeedbackKind::Accepted, 400)); // reset clears throttle
    assert(!feedback.pulse(FeedbackState::kButtons, FeedbackKind::Accepted, 500));
}
