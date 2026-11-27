// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/touch/theme.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace d2d::client::touch {
std::array<int, 3> stamina_colour(std::int64_t current, std::int64_t maximum, bool blue) {
    const bool boosted = maximum + 5 < current || blue;
    if (boosted) maximum = current;
    const auto filled = maximum < 1 ? 0 : current * 102 / maximum;
    if (maximum < 1 || filled < 25) return { 255, 0, 0 };
    return boosted ? std::array<int, 3>{ 0, 0, 255 } : std::array<int, 3>{ 244, 192, 76 };
}

void draw_button_face(std::vector<std::uint8_t>& pixels, int width, int height,
                             float x, float y, float box_width, float box_height,
                             ButtonState state) {
    const float radius = std::min(box_width, box_height) * .5f;
    if (radius <= 1.f || width <= 0 || height <= 0) return;
    const float centre_x = x + box_width * .5f, centre_y = y + box_height * .5f;
    const float outer = radius * radius, outer_rim = (radius - 1.f) * (radius - 1.f);
    const float inner = std::max(0.f, radius - 4.f) * std::max(0.f, radius - 4.f);
    for (int py = std::max(0, int(centre_y - radius)); py < std::min(height, int(centre_y + radius + 1)); ++py)
        for (int px = std::max(0, int(centre_x - radius)); px < std::min(width, int(centre_x + radius + 1)); ++px) {
            const float dx = float(px) + .5f - centre_x, dy = float(py) + .5f - centre_y;
            const float dist = dx * dx + dy * dy;
            if (dist > outer) continue;
            auto* rgba = &pixels[(std::size_t(py) * std::size_t(width) + std::size_t(px)) * 4];
            int red, green, blue;
            if (dist > outer_rim) { red = 28; green = 22; blue = 17; }
            else if (dist > inner) {
                const int light = dy < -dx * .35f ? 24 : -8;
                const int shine = state == ButtonState::Active || state == ButtonState::Pressed ? 24 : 0;
                red = 105 + light + shine; green = 77 + light + shine; blue = 45 + light / 2 + shine / 2;
            } else {
                const unsigned noise = (unsigned(px) * 37u + unsigned(py) * 71u + unsigned(px * py) * 3u) & 7u;
                const int shade = int(noise) + (dy < 0 ? 3 : 0);
                red = 27 + shade; green = 23 + shade; blue = 20 + shade;
                if (state == ButtonState::Pressed) { red += 12; green += 8; blue += 3; }
                if (state == ButtonState::Disabled) { red -= 8; green -= 7; blue -= 6; }
            }
            rgba[0] = std::uint8_t(red); rgba[1] = std::uint8_t(green); rgba[2] = std::uint8_t(blue);
        }
}

void tint_notification_face(std::vector<std::uint8_t>& pixels, int width, int height,
                                   float x, float y, float box_width, float box_height,
                                   const std::array<int, 3>& colour) {
    if (colour == std::array<int, 3>{}) return;
    const float radius = std::min(box_width, box_height) * .5f;
    const float centre_x = x + box_width * .5f, centre_y = y + box_height * .5f;
    for (int py = std::max(0, int(centre_y - radius)); py < std::min(height, int(centre_y + radius + 1)); ++py)
        for (int px = std::max(0, int(centre_x - radius)); px < std::min(width, int(centre_x + radius + 1)); ++px) {
            const float dx = float(px) + .5f - centre_x, dy = float(py) + .5f - centre_y;
            if (dx * dx + dy * dy > radius * radius) continue;
            auto* rgba = &pixels[(std::size_t(py) * std::size_t(width) + std::size_t(px)) * 4];
            const int shade = std::max({ int(rgba[0]), int(rgba[1]), int(rgba[2]) });
            for (int channel = 0; channel < 3; ++channel)
                rgba[channel] = std::uint8_t(colour[std::size_t(channel)] * shade / 255);
        }
}

void shade_button_art(std::vector<std::uint8_t>& pixels, int width, int height,
                             float x, float y, float box_width, float box_height, ButtonState state) {
    if (state != ButtonState::Disabled && state != ButtonState::Pressed) return;
    const float radius = std::min(box_width, box_height) * .5f - 4.f;
    if (radius <= 0.f) return;
    const float centre_x = x + box_width * .5f, centre_y = y + box_height * .5f;
    for (int py = std::max(0, int(centre_y - radius)); py < std::min(height, int(centre_y + radius + 1)); ++py)
        for (int px = std::max(0, int(centre_x - radius)); px < std::min(width, int(centre_x + radius + 1)); ++px) {
            const float dx = float(px) + .5f - centre_x, dy = float(py) + .5f - centre_y;
            if (dx * dx + dy * dy > radius * radius) continue;
            auto* rgba = &pixels[(std::size_t(py) * std::size_t(width) + std::size_t(px)) * 4];
            const int scale = state == ButtonState::Disabled ? 110 : 190;
            for (int channel = 0; channel < 3; ++channel) rgba[channel] = std::uint8_t(int(rgba[channel]) * scale / 255);
        }
}
} // namespace d2d::client::touch
