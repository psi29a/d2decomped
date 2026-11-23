// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/touch/gestures.hpp"

#include "input/gamepad_map.hpp"
#include "input/touch/input.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace d2d::client::touch {

bool HoldGesture::update(std::uint32_t now, std::uint32_t delay) {
    if (finger != -1 && now - started >= delay) activated = true;
    return activated;
}

bool beyond_skill_aim(float dx, float dy) {
    constexpr float slop = 16.f;
    return std::abs(dx) > kSkillAimX / 2 + slop
        || dy < -kSkillAimUp / 2 - slop || dy > kSkillAimDown / 2 + slop;
}

void SkillHold::begin(std::int64_t owner, std::uint32_t now, int unit, float direction_x, float direction_y, bool centered) {
    clear();
    HoldGesture::begin(owner, now);
    target = unit;
    steer(direction_x, direction_y);
    at_caster = centered;
}

void SkillHold::steer(float direction_x, float direction_y) {
    aim_x = direction_x;
    aim_y = direction_y;
}

void SkillHold::resolve(Target& preview, float x, float y, std::span<const pad::Candidate> foes, const pad::Aim& aim) {
    if (at_caster) {
        preview = caster_target(preview.skill, x, y);
        target = -1;
        return;
    }
    const int index = pad::pick_target(x, y, aim_x, aim_y, foes, target, aim);
    target = index < 0 ? -1 : foes[std::size_t(index)].id;
    preview.unit = target;
    preview.x = index < 0 ? x + aim_x : foes[std::size_t(index)].x;
    preview.y = index < 0 ? y + aim_y : foes[std::size_t(index)].y;
    preview.valid = true;
}

bool SkillHold::dispatch(std::uint32_t now, bool ready, std::uint32_t interval) {
    if (!active(now) || !ready || (sent && now - sent_at < interval)) return false;
    sent = true;
    sent_to = target;
    sent_at = now;
    return true;
}

} // namespace d2d::client::touch
