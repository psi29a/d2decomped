// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <protocol.hpp>

namespace d2d::client::skillpad {

// Only touch/controller adapters opt in; pointer and keyboard commands keep defaults.
[[nodiscard]] inline game::cmd::UseSkill direct_cast(int skill, float x, float y, int unit, bool left, int item = -1) { return { skill, x, y, unit, left, item, unit < 0, 1, true }; }

} // namespace d2d::client::skillpad
