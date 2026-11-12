// SPDX-License-Identifier: GPL-3.0-or-later
// The gauntlet as an OS cursor image (deviations.md improvement 19): game.exe
// hides the Windows pointer (it imports ShowCursor and LoadCursorA, never
// SetCursor or CreateCursor) and draws CURSOR\ohand.dc6 into each frame, so
// the hand lags a frame behind the mouse. d2d hands the same pixels to the
// OS, which moves them with the mouse between frames.
#pragma once

#include <dc6.hpp>
#include <palette.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace d2d::client {

inline bool g_hw_cursor = true;   // --toggle hwcursor=off: draw the gauntlet into the frame, as game.exe

struct CursorImage {
    int width = 0, height = 0;
    int hot_x = 0, hot_y = 0;             // the pixel on the mouse position
    std::vector<std::uint8_t> rgba;       // width * height * 4, index 0 clear
    bool operator==(const CursorImage&) const = default;
};

// How the window's scalemode samples this image. Pixel art and nearest take
// one source pixel. Linear blends the four pixels round the sample.
enum class CursorScaleMode { pixelart, nearest, linear };

// The frame through `pal`, scaled `scale` times and placed as blit_sprite
// places it: the frame's top-left at the mouse position plus
// (offset_x, offset_y - height + 1), so that offset gives the hot spot.
// `scale_mode` defaults to nearest, which is also what pixel art uses.
inline CursorImage cursor_image(const d2d::dc6::Frame& frame, const d2d::palette::Palette& pal, float scale,
                                CursorScaleMode scale_mode = CursorScaleMode::nearest) {
    CursorImage image;
    if (frame.width == 0 || frame.height == 0 || scale <= 0) return image;
    image.width  = std::max(1, int(std::lround(float(frame.width) * scale)));
    image.height = std::max(1, int(std::lround(float(frame.height) * scale)));
    image.rgba.assign(std::size_t(image.width) * std::size_t(image.height) * 4, 0);
    if (scale_mode == CursorScaleMode::linear) {
        // Sample at the output pixel's centre, clamped to the source edge,
        // and blend the channels. Index 0 contributes nothing (clear).
        const auto texel = [&](int src_row, int src_col) -> std::array<float, 4> {
            const auto index = frame.pixels[std::size_t(src_row) * frame.width + std::size_t(src_col)];
            if (index == 0) return std::array<float, 4>{};
            const auto colour = pal[index];
            return std::array<float, 4>{ float(colour.r), float(colour.g), float(colour.b), 255.f };
        };
        for (int row = 0; row < image.height; ++row) {
            const float src_y = (float(row) + 0.5f) * float(frame.height) / float(image.height) - 0.5f;
            const float clamped_y = std::clamp(src_y, 0.f, float(frame.height - 1));
            const int top = int(std::floor(clamped_y));
            const int bottom = std::min(top + 1, int(frame.height) - 1);
            const float weight_y = clamped_y - float(top);
            for (int col = 0; col < image.width; ++col) {
                const float src_x = (float(col) + 0.5f) * float(frame.width) / float(image.width) - 0.5f;
                const float clamped_x = std::clamp(src_x, 0.f, float(frame.width - 1));
                const int left = int(std::floor(clamped_x));
                const int right = std::min(left + 1, int(frame.width) - 1);
                const float weight_x = clamped_x - float(left);
                const auto top_left = texel(top, left);
                const auto top_right = texel(top, right);
                const auto bottom_left = texel(bottom, left);
                const auto bottom_right = texel(bottom, right);
                auto* out = &image.rgba[(std::size_t(row) * std::size_t(image.width) + std::size_t(col)) * 4];
                for (int channel = 0; channel < 4; ++channel) {
                    const float upper = top_left[channel] + (top_right[channel] - top_left[channel]) * weight_x;
                    const float lower = bottom_left[channel] + (bottom_right[channel] - bottom_left[channel]) * weight_x;
                    out[channel] = std::uint8_t(std::clamp(int(std::lround(upper + (lower - upper) * weight_y)), 0, 255));
                }
            }
        }
    } else {
        for (int row = 0; row < image.height; ++row) {
            const auto src_row = std::size_t(row) * frame.height / std::size_t(image.height);
            for (int col = 0; col < image.width; ++col) {
                const auto src_col = std::size_t(col) * frame.width / std::size_t(image.width);
                const auto index = frame.pixels[src_row * frame.width + src_col];
                if (index == 0) continue;
                const auto colour = pal[index];
                auto* out = &image.rgba[(std::size_t(row) * std::size_t(image.width) + std::size_t(col)) * 4];
                out[0] = colour.r; out[1] = colour.g; out[2] = colour.b; out[3] = 255;
            }
        }
    }
    image.hot_x = std::clamp(int(std::lround(float(-frame.offset_x) * scale)), 0, image.width - 1);
    image.hot_y = std::clamp(int(std::lround(float(int(frame.height) - 1 - frame.offset_y) * scale)), 0, image.height - 1);
    return image;
}

}  // namespace d2d::client
