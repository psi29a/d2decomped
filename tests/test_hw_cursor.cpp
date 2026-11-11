// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: palette dc6
// test-includes: apps/d2d
// The gauntlet's OS cursor image (apps/d2d/hw_cursor.hpp): its hot spot is
// the pixel blit_sprite puts on the mouse, at any scale.
#include "hw_cursor.hpp"

#include <dc6.hpp>
#include <palette.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    std::array<d2d::palette::Rgba, 256> colours{};
    colours[7] = { 10, 20, 30, 255 };
    const d2d::palette::Palette pal{ colours };
    // 3 x 2, drawn with its top-left at mouse + (offset_x, offset_y - height + 1):
    // offset_x -1 puts the mouse on column 1, offset_y 0 on the bottom row.
    d2d::dc6::Frame frame;
    frame.width = 3; frame.height = 2;
    frame.offset_x = -1; frame.offset_y = 0;
    frame.pixels = { 0, 7, 7,
                     7, 7, 0 };
    const auto one = d2d::client::cursor_image(frame, pal, 1.f);
    assert(one.width == 3 && one.height == 2);
    assert(one.hot_x == 1 && one.hot_y == 1);                    // column 1 of the bottom row
    assert(one.rgba[3] == 0);                                     // index 0 is clear
    assert(one.rgba[4] == 10 && one.rgba[5] == 20 && one.rgba[6] == 30 && one.rgba[7] == 255);
    const auto two = d2d::client::cursor_image(frame, pal, 2.f);
    assert(two.width == 6 && two.height == 4);
    assert(two.hot_x == 2 && two.hot_y == 2);
    assert(two.rgba[(3 * 6 + 5) * 4 + 3] == 0 && two.rgba[(3 * 6 + 3) * 4 + 3] == 255);   // nearest: bottom row ends clear
    // Pixel art samples like nearest. The default argument is that sample too.
    const auto pixelart = d2d::client::cursor_image(frame, pal, 2.f, d2d::client::CursorScaleMode::pixelart);
    const auto nearest = d2d::client::cursor_image(frame, pal, 2.f, d2d::client::CursorScaleMode::nearest);
    assert(pixelart == two && nearest == two);
    // Linear blends pixel centres. Column 1 of the top row is a quarter of
    // the way from the clear pixel to palette 7: (3, 5, 8, 64). Nearest
    // leaves that pixel clear. Column 3 of the bottom row blends palette 7
    // toward the clear corner: (8, 15, 23, 191), where nearest copies it.
    const auto linear = d2d::client::cursor_image(frame, pal, 2.f, d2d::client::CursorScaleMode::linear);
    assert(linear.width == 6 && linear.height == 4);
    assert(linear.hot_x == 2 && linear.hot_y == 2);
    assert(linear.rgba[(0 * 6 + 0) * 4 + 3] == 0 && linear.rgba[(3 * 6 + 5) * 4 + 3] == 0);
    assert(linear.rgba[(0 * 6 + 1) * 4 + 0] == 3 && linear.rgba[(0 * 6 + 1) * 4 + 1] == 5);
    assert(linear.rgba[(0 * 6 + 1) * 4 + 2] == 8 && linear.rgba[(0 * 6 + 1) * 4 + 3] == 64);
    assert(two.rgba[(0 * 6 + 1) * 4 + 3] == 0);
    assert(linear.rgba[(3 * 6 + 3) * 4 + 0] == 8 && linear.rgba[(3 * 6 + 3) * 4 + 1] == 15);
    assert(linear.rgba[(3 * 6 + 3) * 4 + 2] == 23 && linear.rgba[(3 * 6 + 3) * 4 + 3] == 191);
    assert(d2d::client::cursor_image(frame, pal, 1.f, d2d::client::CursorScaleMode::linear) == one);
    // A hot spot outside the frame is clamped into it, as SDL needs.
    frame.offset_x = 2;
    assert(d2d::client::cursor_image(frame, pal, 1.f).hot_x == 0);
    std::printf("OK: the cursor image keeps blit_sprite's hot spot\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
