// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "input/gamepad_map.hpp"
#include "input/touch/input.hpp"

#include <cstdint>
#include <span>

namespace d2d::client::touch {

// A held gesture activates only after a short tap has had time to finish.
struct HoldGesture {
    std::int64_t finger = -1;
    std::uint32_t started = 0;
    bool activated = false;

    void begin(std::int64_t owner, std::uint32_t now) { finger = owner; started = now; activated = false; }
    bool update(std::uint32_t now, std::uint32_t delay = 300);
    void clear() { finger = -1; activated = false; }
};

// Screen-space limits of the two-to-one drag projection. Allow a little
// overshoot at its edge before interpreting a drag as cancellation.
inline constexpr float kSkillAimX = 370.f, kSkillAimUp = 250.f, kSkillAimDown = 210.f;
bool beyond_skill_aim(float dx, float dy);

struct SkillHold : HoldGesture {
    int target = -1;
    float aim_x = 0, aim_y = 0;
    bool cancelled = false, chases = false, at_caster = false, sent = false;
    int sent_to = -1;
    std::uint32_t sent_at = 0;

    void begin(std::int64_t owner, std::uint32_t now, int unit, float direction_x, float direction_y, bool centered = false);
    void steer(float direction_x, float direction_y);
    void cancel() { cancelled = true; }
    [[nodiscard]] bool release_casts() const { return !activated && !cancelled; }
    bool active(std::uint32_t now) { return !cancelled && update(now); }
    void resolve(Target& preview, float x, float y, std::span<const pad::Candidate> foes, const pad::Aim& aim);
    bool dispatch(std::uint32_t now, bool ready, std::uint32_t interval);
    void clear() { *this = {}; }
};

} // namespace d2d::client::touch
