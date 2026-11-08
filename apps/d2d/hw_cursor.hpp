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

// The frame through `pal`, scaled `scale` times (nearest pixel) and placed as
// blit_sprite places it: the frame's top-left at the mouse position plus
// (offset_x, offset_y - height + 1), so that offset gives the hot spot.
// ponytail: nearest only, whatever scalemode the game's frame uses.
inline CursorImage cursor_image(const d2d::dc6::Frame& frame, const d2d::palette::Palette& pal, float scale) {
    CursorImage image;
    if (frame.width == 0 || frame.height == 0 || scale <= 0) return image;
    image.width  = std::max(1, int(std::lround(float(frame.width) * scale)));
    image.height = std::max(1, int(std::lround(float(frame.height) * scale)));
    image.rgba.assign(std::size_t(image.width) * std::size_t(image.height) * 4, 0);
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
    image.hot_x = std::clamp(int(std::lround(float(-frame.offset_x) * scale)), 0, image.width - 1);
    image.hot_y = std::clamp(int(std::lround(float(int(frame.height) - 1 - frame.offset_y) * scale)), 0, image.height - 1);
    return image;
}

}  // namespace d2d::client
