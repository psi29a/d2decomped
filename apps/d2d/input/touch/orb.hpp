// SPDX-License-Identifier: GPL-3.0-or-later
// Small nearest-neighbour compositor for the native indexed HP / MP globes.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace d2d::client::touch {

struct OrbFrame {
    std::span<const std::uint8_t> pixels;
    int width = 0;
    int height = 0;
};

struct OrbColour {
    std::uint8_t red = 0, green = 0, blue = 0, alpha = 255;
};

/**
 * Draw a scaled native globe into an RGBA framebuffer. Row counts are in
 * source-frame pixels and are computed by the caller using the native HUD
 * rules (globe_preview_rows and the living-life minimum). Pixel index zero
 * stays transparent. Preview is quarter blended first, fill is opaque next,
 * and glass is composited last. The palette callback maps an index to RGBA.
 */
template <class PaletteLookup>
inline void draw_indexed_orb(std::span<std::uint8_t> framebuffer,
                             int framebuffer_width, int framebuffer_height, int stride_bytes,
                             int x, int y, int diameter,
                             OrbFrame fill, int fill_rows,
                             OrbFrame preview, int preview_rows,
                             OrbFrame glass, PaletteLookup&& palette_lookup) {
    if (framebuffer_width <= 0 || framebuffer_height <= 0 || stride_bytes < framebuffer_width * 4
        || diameter <= 0 || fill.width <= 0 || fill.height <= 0
        || fill.pixels.size() < std::size_t(fill.width) * std::size_t(fill.height)
        || glass.width <= 0 || glass.height <= 0
        || glass.pixels.size() < std::size_t(glass.width) * std::size_t(glass.height)) return;

    const bool has_preview = preview.width > 0 && preview.height > 0
        && preview.pixels.size() >= std::size_t(preview.width) * std::size_t(preview.height);
    fill_rows = std::clamp(fill_rows, 0, fill.height);
    preview_rows = std::clamp(preview_rows, fill_rows, fill.height);
    const int left = std::max(x, 0), top = std::max(y, 0);
    const int right = std::min(x + diameter, framebuffer_width);
    const int bottom = std::min(y + diameter, framebuffer_height);
    if (left >= right || top >= bottom || framebuffer.size() < std::size_t(stride_bytes) * std::size_t(framebuffer_height)) return;

    const auto blend_preview = [](std::uint8_t foreground, std::uint8_t background) {
        return std::uint8_t((unsigned(foreground) + 3u * unsigned(background)) / 4u);
    };
    const auto plot = [&](int pixel_x, int pixel_y, OrbFrame frame, int source_x, int source_y, bool quarter) {
        const auto index = frame.pixels[std::size_t(source_y) * std::size_t(frame.width) + std::size_t(source_x)];
        if (index == 0) return;
        const OrbColour colour = palette_lookup(index);
        auto* pixel = framebuffer.data() + std::size_t(pixel_y) * std::size_t(stride_bytes) + std::size_t(pixel_x) * 4;
        if (quarter) {
            pixel[0] = blend_preview(colour.red, pixel[0]);
            pixel[1] = blend_preview(colour.green, pixel[1]);
            pixel[2] = blend_preview(colour.blue, pixel[2]);
        } else {
            pixel[0] = colour.red; pixel[1] = colour.green; pixel[2] = colour.blue;
            pixel[3] = colour.alpha;
        }
    };

    for (int pixel_y = top; pixel_y < bottom; ++pixel_y) {
        const int dy = pixel_y - y;
        const int fill_y = dy * fill.height / diameter;
        for (int pixel_x = left; pixel_x < right; ++pixel_x) {
            const int dx = pixel_x - x;
            const int fill_x = dx * fill.width / diameter;
            // The source's final rows are the bottom of the liquid.
            if (has_preview && fill_y >= fill.height - preview_rows && fill_y < fill.height - fill_rows) {
                const int preview_x = dx * preview.width / diameter;
                const int preview_y = dy * preview.height / diameter;
                plot(pixel_x, pixel_y, preview, preview_x, preview_y, true);
            }
            if (fill_y >= fill.height - fill_rows) plot(pixel_x, pixel_y, fill, fill_x, fill_y, false);
            const int glass_x = dx * glass.width / diameter;
            const int glass_y = dy * glass.height / diameter;
            plot(pixel_x, pixel_y, glass, glass_x, glass_y, false);
        }
    }
}

} // namespace d2d::client::touch
