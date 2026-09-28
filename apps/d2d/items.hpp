// Item hover text drawn (the lines: item_text.hpp).
#pragma once

#include "world.hpp"

namespace d2d::client {

// Hover text box: lines centred over [x0, x1], bottom on `bottom` (below
// `top` instead when it would leave the screen), on a darkened backdrop.
void draw_hover_text(std::vector<std::uint8_t>& fb, const Scene& s, const std::vector<TextLine>& lines,
                     int x0, int x1, int top, int bottom);

}  // namespace d2d::client
