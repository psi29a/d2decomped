// SPDX-License-Identifier: GPL-3.0-or-later
// Item hover text drawn (the lines: item_text.hpp).
#pragma once

#include "common.hpp"
#include "scene.hpp"

#include <cstdint>
#include <vector>

namespace d2d::client {

// Hover text box: lines centred over [x0, x1], bottom on `bottom` (below
// `top` instead when it would leave the screen), on a darkened backdrop.
void draw_hover_text(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const std::vector<TextLine>& lines,
                     int left, int right, int top, int bottom);

}  // namespace d2d::client
