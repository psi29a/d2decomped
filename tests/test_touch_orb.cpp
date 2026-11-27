// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// Native indexed globe composition, scaled and clipped without game assets.
#include "input/touch/orb.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

int main() try {
    using namespace d2d::client::touch;
    constexpr int width = 6, height = 4, stride = width * 4;
    std::vector<std::uint8_t> framebuffer(std::size_t(stride * height), 20);
    const std::array<std::uint8_t, 16> liquid{
        1, 1, 1, 1,
        1, 1, 1, 1,
        1, 1, 1, 1,
        1, 1, 1, 1,
    };
    const std::array<std::uint8_t, 16> preview{
        2, 2, 2, 2,
        2, 2, 2, 2,
        2, 2, 2, 2,
        2, 2, 2, 2,
    };
    const std::array<std::uint8_t, 16> glass{
        3, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 3,
    };
    const auto colour = [](std::uint8_t index) {
        if (index == 1) return OrbColour{ 200, 0, 0, 255 };
        if (index == 2) return OrbColour{ 0, 0, 200, 255 };
        return OrbColour{ 0, 200, 0, 255 };
    };
    draw_indexed_orb(framebuffer, width, height, stride, 1, 0, 4,
                     { liquid, 4, 4 }, 2, { preview, 4, 4 }, 3, { glass, 4, 4 }, colour);

    const auto pixel = [&](int x, int y) {
        const auto offset = std::size_t(y * stride + x * 4);
        return std::array<std::uint8_t, 4>{ framebuffer[offset], framebuffer[offset + 1], framebuffer[offset + 2], framebuffer[offset + 3] };
    };
    assert((pixel(1, 0) == std::array<std::uint8_t, 4>{ 0, 200, 0, 255 })); // transparent fill, glass last
    assert((pixel(2, 0) == std::array<std::uint8_t, 4>{ 20, 20, 20, 20 })); // empty and transparent
    assert((pixel(2, 1) == std::array<std::uint8_t, 4>{ 15, 15, 65, 20 })); // quarter preview blend
    assert((pixel(2, 2) == std::array<std::uint8_t, 4>{ 200, 0, 0, 255 })); // liquid fills from bottom
    assert((pixel(4, 3) == std::array<std::uint8_t, 4>{ 0, 200, 0, 255 })); // glass overlays fill
    assert((pixel(0, 0) == std::array<std::uint8_t, 4>{ 20, 20, 20, 20 })); // outside the orb

    // A 48-byte stride leaves two padding pixels beyond the 10-pixel view.
    // The orb extends beyond the right edge; only visible framebuffer pixels
    // change, and row padding remains untouched.
    constexpr int wide_width = 10, wide_height = 2, wide_stride = 48;
    std::vector<std::uint8_t> wide(std::size_t(wide_stride * wide_height), 77);
    const std::array<std::uint8_t, 4> solid{ 1, 1, 1, 1 };
    const std::array<std::uint8_t, 4> clear{};
    draw_indexed_orb(wide, wide_width, wide_height, wide_stride, 8, 0, 4,
                     { solid, 2, 2 }, 2, {}, 0, { clear, 2, 2 }, colour);
    for (int y = 0; y < wide_height; ++y) {
        for (int x = 0; x < wide_width; ++x) {
            const auto offset = std::size_t(y * wide_stride + x * 4);
            if (x >= 8) assert(wide[offset] == 200 && wide[offset + 3] == 255);
            else assert(wide[offset] == 77 && wide[offset + 3] == 77);
        }
        for (int offset = wide_width * 4; offset < wide_stride; ++offset)
            assert(wide[std::size_t(y * wide_stride + offset)] == 77);
    }
    return 0;
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
