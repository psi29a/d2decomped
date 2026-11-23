// SPDX-License-Identifier: GPL-3.0-or-later
// Where a frame's poll of SDL events ends (pointer_events.cpp).
#pragma once

#include "ui.hpp"

#include <cstdint>

namespace d2d::client {

// Whether an event of SDL type `next_type` waits for the next frame: a
// press is in this frame's snapshot and the event would move the pointer
// or end the press, so every reader tests the press where it was (#545).
[[nodiscard]] bool waits_for_next_frame(const Mouse& mouse, std::uint32_t next_type);

}  // namespace d2d::client
