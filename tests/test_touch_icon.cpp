// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: palette
// test-includes: apps/d2d
#include "touch_icon.hpp"

#include <palette.hpp>

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <span>
#include <vector>

int main() try {
    using namespace d2d::client::touch;
    const auto square = circular_icon_rect(10, 20, 48, 48, 64, 32);
    assert(square.width == 42 && square.height == 21);
    assert(std::abs(square.scale - 42.f / 64.f) < .001f);
    assert(square.x == 13 && square.y == 34);
    const auto wide = circular_icon_rect(0, 0, 80, 48, 32, 64);
    assert(wide.width == 21 && wide.height == 42); // circle is bounded by the short side
    assert(inside_icon_circle(23, 43, 0, 0, 48, 48));
    assert(!inside_icon_circle(0, 0, 0, 0, 48, 48));
    assert(!inside_icon_circle(24, 24, 0, 0, 48, 48, 24)); // no area inside a full rim
    assert(circular_icon_rect(0, 0, 8, 8, 16, 16, 4).scale == 0.f);
    const auto palette = [](std::uint8_t index) { return d2d::palette::Rgba{ index, 70, 90, 255 }; };
    const std::array<std::uint8_t, 8> art{ 0, 1, 1, 0, 2, 2, 2, 2 };
    std::vector<std::uint8_t> pixels(48 * 48 * 4, 17);
    draw_circular_icon(pixels, 48, 48, 0, 0, 48, 48, art, 4, 2, palette);
    bool drawn = false;
    for (int row = 0; row < 48; ++row)
        for (int column = 0; column < 48; ++column) {
            const auto offset = (std::size_t(row) * 48 + std::size_t(column)) * 4;
            if (!inside_icon_circle(column, row, 0, 0, 48, 48)) assert(pixels[offset] == 17);
            if (pixels[offset] != 17) { drawn = true; assert(pixels[offset + 3] == 255); }
        }
    assert(drawn && pixels[0] == 17);
    const auto original = pixels;
    const std::array<std::uint8_t, 8> transparent{};
    draw_circular_icon(pixels, 48, 48, 0, 0, 48, 48, transparent, 4, 2, palette);
    assert(pixels == original);
    draw_circular_icon(pixels, 48, 48, 80, 80, 48, 48, art, 4, 2, palette);
    assert(pixels == original);
    draw_circular_icon(pixels, 48, 48, -24, -24, 48, 48, art, 4, 2, palette);
    assert(pixels != original); // A partially off-screen icon still draws its visible portion.
    const auto clipped = pixels;
    draw_circular_icon(pixels, 48, 48, 0, 0, 48, 48, std::span<const std::uint8_t>{}, 4, 2, palette);
    assert(pixels == clipped);

}
 catch (const std::exception& error) {
    std::fprintf(stderr, "touch icon regression: %s\n", error.what());
    return 1;
}
