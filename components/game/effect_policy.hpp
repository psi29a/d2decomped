// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>
#include <utility>
#include <vector>

namespace d2d::game {
// Conservative allowlist for the optional, host-controlled dismissal UI.
bool dismissible_self_effect(std::string_view name);
bool dismissible_shrine_effect(int code, const std::vector<std::pair<int, int>>& stats);
} // namespace d2d::game
