// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace d2d::client::touch {

enum class FeedbackKind : std::uint8_t { None, Accepted, Rejected, Cancelled, Pressed };

struct FeedbackSample {
    FeedbackKind kind = FeedbackKind::None;
    float strength = 0.f;
    float phase = 0.f;
    bool active = false;
};

/** Small independent timed feedback channels for stable touch button IDs. */
class FeedbackState {
public:
    static constexpr std::size_t kButtons = 17;
    static constexpr std::uint32_t kPulseRateLimitMs = 180;

    /** Start an accepted, rejected, or cancelled pulse; accepted/rejected are rate limited. */
    bool pulse(std::size_t button, FeedbackKind kind, std::uint32_t now_ms) {
        if (button >= kButtons || (kind != FeedbackKind::Accepted && kind != FeedbackKind::Rejected &&
                                   kind != FeedbackKind::Cancelled)) return false;
        auto& state = states_[button];
        if (kind == FeedbackKind::Accepted || kind == FeedbackKind::Rejected) {
            if (state.has_rate_time && std::uint32_t(now_ms - state.last_rate_time) < kPulseRateLimitMs)
                return false;
            state.last_rate_time = now_ms;
            state.has_rate_time = true;
        }
        state.kind = kind;
        state.started = now_ms;
        state.duration = kind == FeedbackKind::Rejected ? 300u : kind == FeedbackKind::Accepted ? 220u : 180u;
        state.pressed = false;
        return true;
    }

    /** Keep feedback lit while this button is held. */
    void press(std::size_t button, std::uint32_t now_ms) {
        if (button >= kButtons) return;
        auto& state = states_[button];
        state.kind = FeedbackKind::Pressed;
        state.started = now_ms;
        state.duration = 0;
        state.pressed = true;
    }

    /** Clear one button after release or cancellation. */
    void release(std::size_t button) {
        if (button >= kButtons) return;
        states_[button].kind = FeedbackKind::None;
        states_[button].pressed = false;
        states_[button].duration = 0;
    }

    /** Clear every visible state and pulse throttle, for panel changes or focus loss. */
    void reset() { states_ = {}; }

    FeedbackSample sample(std::size_t button, std::uint32_t now_ms) const {
        if (button >= kButtons) return {};
        const auto& state = states_[button];
        if (state.kind == FeedbackKind::None) return {};
        if (state.pressed) return { FeedbackKind::Pressed, 1.f, 0.f, true };
        const std::uint32_t elapsed = std::uint32_t(now_ms - state.started);
        if (elapsed >= state.duration) return {};
        const float phase = float(elapsed) / float(state.duration);
        return { state.kind, 1.f - phase, phase, true };
    }

private:
    struct ButtonState {
        FeedbackKind kind = FeedbackKind::None;
        std::uint32_t started = 0;
        std::uint32_t duration = 0;
        std::uint32_t last_rate_time = 0;
        bool has_rate_time = false;
        bool pressed = false;
    };
    std::array<ButtonState, kButtons> states_{};
};

} // namespace d2d::client::touch
