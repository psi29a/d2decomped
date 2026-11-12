// SPDX-License-Identifier: GPL-3.0-or-later
// How long an overhead message stays (FUN_00661110, Chat/Chat.cpp).
// The text length is clamped to 254, then the stay is length * 8 + 125
// frames. An empty text makes no node; callers skip it.
#pragma once

#include <algorithm>
#include <cstddef>

namespace d2d::client {

[[nodiscard]] inline int overhead_frames(std::size_t length) {
    const auto shown = std::min(length, std::size_t{254});
    return int(shown * 8 + 125);
}

}  // namespace d2d::client
