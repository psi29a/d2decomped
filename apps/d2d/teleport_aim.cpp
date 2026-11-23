// SPDX-License-Identifier: GPL-3.0-or-later
#include "teleport_aim.hpp"

#include "touch/input.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace d2d::client {

std::pair<float, float> teleport_stick_drag(float x, float y, float magnitude) {
    const float amount = std::clamp(magnitude, 0.f, 1.f);
    return { x * amount * 185.f, y * amount * 125.f };
}

bool TeleportAim::matches(const touch::Target& current) const {
    return shown && displayed.valid && current.valid && displayed.skill == current.skill
        && std::hypot(displayed.x - current.x, displayed.y - current.y) < .01f;
}

} // namespace d2d::client
