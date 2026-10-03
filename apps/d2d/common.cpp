// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for common.hpp: sprite blits, the test pattern.
#include "common.hpp"

#include <dc6.hpp>
#include <palette.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace d2d::client {

void blit_sprite(std::vector<std::uint8_t>& framebuffer,
                 const d2d::dc6::Frame& frame,
                 const d2d::palette::Palette& pal,
                 int dst_x, int dst_y, int shade) {
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        const int dy = dst_y + int(y);
        if (dy < 0 || dy >= int(kScreenHeight)) continue;
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const int dx = dst_x + int(x);
            if (dx < 0 || dx >= int(kScreenWidth)) continue;
            const auto idx = frame.pixels[y * frame.width + x];
            if (idx == 0) continue;
            const auto colour = pal[idx];
            auto* pixel = &framebuffer[(std::size_t(dy) * kScreenWidth + std::size_t(dx)) * 4];
            if (shade == 256) { pixel[0] = colour.r; pixel[1] = colour.g; pixel[2] = colour.b; }
            else {
                pixel[0] = std::uint8_t(std::min(255, colour.r * shade / 256));
                pixel[1] = std::uint8_t(std::min(255, colour.g * shade / 256));
                pixel[2] = std::uint8_t(std::min(255, colour.b * shade / 256));
            }
            pixel[3] = colour.a;
        }
    }
}

void blit_at_anchor(std::vector<std::uint8_t>& framebuffer,
                    const d2d::dc6::Frame& frame,
                    const d2d::palette::Palette& pal,
                    int anchor_x, int anchor_y) {
    const int x = anchor_x + frame.offset_x;
    const int y = anchor_y + frame.offset_y - int(frame.height) + 1;
    blit_sprite(framebuffer, frame, pal, x, y);
}

void blit_additive(std::vector<std::uint8_t>& framebuffer,
                   const d2d::dc6::Frame& frame,
                   const d2d::palette::Palette& pal,
                   const d2d::palette::Pl2* pl2,
                   int anchor_x, int anchor_y) {
    const int dst_x = anchor_x + frame.offset_x;
    const int dst_y = anchor_y + frame.offset_y - int(frame.height) + 1;
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        const int pixel_y = dst_y + int(y);
        if (pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const int pixel_x = dst_x + int(x);
            if (pixel_x < 0 || pixel_x >= int(kScreenWidth)) continue;
            const auto idx = frame.pixels[y * frame.width + x];
            if (idx == 0) continue;
            auto* pixel = &framebuffer[(std::size_t(pixel_y) * kScreenWidth + std::size_t(pixel_x)) * 4];
            if (pl2 && pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0) {
                // Fast path: black bg => additive result == fg palette entry.
                const auto& out = pl2->base_palette()[idx];
                pixel[0] = out.r; pixel[1] = out.g; pixel[2] = out.b;
            } else {
                const auto colour = pal[idx];
                pixel[0] = std::uint8_t(std::min(255, int(pixel[0]) + int(colour.r)));
                pixel[1] = std::uint8_t(std::min(255, int(pixel[1]) + int(colour.g)));
                pixel[2] = std::uint8_t(std::min(255, int(pixel[2]) + int(colour.b)));
            }
        }
    }
}

void paint_test_pattern(std::vector<std::uint8_t>& framebuffer) {
    // Diagonal gradient — a visually distinctive canary when no MPQ loads.
    for (std::uint32_t y = 0; y < kScreenHeight; ++y) {
        for (std::uint32_t x = 0; x < kScreenWidth; ++x) {
            auto* pixel = &framebuffer[(y * kScreenWidth + x) * 4];
            pixel[0] = std::uint8_t(x);
            pixel[1] = std::uint8_t(y);
            pixel[2] = std::uint8_t((x + y) / 2);
            pixel[3] = 0xFF;
        }
    }
}

void blit_dc6_grid(std::vector<std::uint8_t>& framebuffer,
                   const d2d::dc6::Sprite& spr,
                   const d2d::palette::Palette& pal,
                   int origin_x, int origin_y,
                   int tiles_across) {
    const auto per_dir = spr.frames_per_direction();
    int cell_y = origin_y;
    int cell_x = origin_x;
    int row_h = 0;
    for (int i = 0; i < int(per_dir); ++i) {
        const auto& frame = spr.frame(0, i);
        blit_sprite(framebuffer, frame, pal, cell_x, cell_y);
        cell_x += int(frame.width);
        if (int(frame.height) > row_h) row_h = int(frame.height);
        if ((i + 1) % tiles_across == 0) {
            cell_x  = origin_x;
            cell_y += row_h;
            row_h = 0;
        }
    }
}

}  // namespace d2d::client
