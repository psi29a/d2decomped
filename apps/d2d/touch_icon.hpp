// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace d2d::client::touch {

struct IconRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    float scale = 0.f;
};

/** Fit art inside the button's circular interior without changing its aspect ratio. */
inline IconRect circular_icon_rect(float x, float y, float width, float height,
                                   float art_width, float art_height, float rim = 3.f) {
    if (width <= 0.f || height <= 0.f || art_width <= 0.f || art_height <= 0.f) return {};
    const float diameter = std::max(0.f, std::min(width, height) - 2.f * rim);
    if (diameter <= 0.f) return {};
    const float scale = diameter / std::max(art_width, art_height);
    const int dest_width = std::max(1, int(std::lround(art_width * scale)));
    const int dest_height = std::max(1, int(std::lround(art_height * scale)));
    return { int(std::lround(x + (width - dest_width) * .5f)),
             int(std::lround(y + (height - dest_height) * .5f)), dest_width, dest_height, scale };
}

/** Pixel-center clip against the button's circular interior, leaving its rim visible. */
inline bool inside_icon_circle(int pixel_x, int pixel_y, float x, float y,
                               float width, float height, float rim = 3.f) {
    const float radius = std::max(0.f, std::min(width, height) * .5f - rim);
    const float dx = float(pixel_x) + .5f - (x + width * .5f);
    const float dy = float(pixel_y) + .5f - (y + height * .5f);
    return radius > 0.f && dx * dx + dy * dy <= radius * radius;
}

// Nearest-neighbor indexed art, clipped inside the control's rim. Palette index zero is transparent.
template <typename ColourAt>
void draw_circular_icon(std::vector<std::uint8_t>& pixels, int width, int height,
                        float x, float y, float box_width, float box_height,
                        std::span<const std::uint8_t> art, int art_width, int art_height,
                        ColourAt colour_at, float rim = 3.f) {
    if (width <= 0 || height <= 0 || art_width <= 0 || art_height <= 0
        || pixels.size() < std::size_t(width) * std::size_t(height) * 4
        || art.size() < std::size_t(art_width) * std::size_t(art_height)) return;
    const auto destination = circular_icon_rect(x, y, box_width, box_height, float(art_width), float(art_height), rim);
    if (destination.scale <= 0.f) return;
    for (int row = std::max(0, destination.y); row < std::min(height, destination.y + destination.height); ++row)
        for (int column = std::max(0, destination.x); column < std::min(width, destination.x + destination.width); ++column) {
            if (!inside_icon_circle(column, row, x, y, box_width, box_height, rim)) continue;
            const int source_x = std::min(art_width - 1, int(float(column - destination.x) / destination.scale));
            const int source_y = std::min(art_height - 1, int(float(row - destination.y) / destination.scale));
            const auto index = art[std::size_t(source_y) * std::size_t(art_width) + std::size_t(source_x)];
            if (index == 0) continue;
            const auto colour = colour_at(index);
            auto* output = &pixels[(std::size_t(row) * std::size_t(width) + std::size_t(column)) * 4];
            output[0] = colour.r; output[1] = colour.g; output[2] = colour.b; output[3] = colour.a;
        }
}

} // namespace d2d::client::touch
