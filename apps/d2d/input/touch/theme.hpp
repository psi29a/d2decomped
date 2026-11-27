// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace d2d::client::touch {

// Match the native HUD's stamina colour thresholds and requested palette colours.
std::array<int, 3> stamina_colour(std::int64_t current, std::int64_t maximum, bool blue);

inline bool points_available(std::int64_t points) { return points > 0; }

// Pick the strongest red actually present in the native level notification art.
// Index zero is transparent in DC6 sprites. An empty result means the art or
// its palette was unavailable, so callers can retain the normal styling.
template <typename Colour>
inline std::array<int, 3> notification_colour(std::span<const std::uint8_t> indices,
                                               std::span<const Colour> palette) {
    std::array<int, 3> result{};
    int best = 0;
    for (const auto index : indices) {
        if (index == 0 || std::size_t(index) >= palette.size()) continue;
        const auto& colour = palette[index];
        const int score = int(colour.r) - std::max(int(colour.g), int(colour.b));
        if (score > best) { best = score; result = { colour.r, colour.g, colour.b }; }
    }
    return result;
}

enum class ButtonState { Normal, Active, Pressed, Disabled };

// Draw a deterministic dark stone face with a directional, aged bronze bevel.
// Squared distances keep the per-pixel work small and clip naturally at the screen edge.
void draw_button_face(std::vector<std::uint8_t>& pixels, int width, int height,
                             float x, float y, float box_width, float box_height,
                             ButtonState state = ButtonState::Normal);

// Carry the notification artwork's red through the stone face and bevel,
// keeping their existing shading so the control remains legible.
void tint_notification_face(std::vector<std::uint8_t>& pixels, int width, int height,
                                   float x, float y, float box_width, float box_height,
                                   const std::array<int, 3>& colour);

// Tint artwork without changing the bevel; normal artwork stays exactly as supplied.
void shade_button_art(std::vector<std::uint8_t>& pixels, int width, int height,
                             float x, float y, float box_width, float box_height, ButtonState state);

} // namespace d2d::client::touch
