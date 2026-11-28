// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: palette
// test-includes: apps/d2d
// test-sources: apps/d2d/input/touch/theme.cpp
#include "input/touch/theme.hpp"

#include <array>
#include <palette.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <span>
#include <utility>
#include <vector>

int main() try {
    using namespace d2d::client::touch;
    assert(!points_available(0) && !points_available(-1) && points_available(1));
    assert((stamina_colour(100, 100, false) == std::array<int, 3>{ 244, 192, 76 }));
    assert((stamina_colour(24, 100, false) == std::array<int, 3>{ 255, 0, 0 }));
    assert((stamina_colour(0, 0, false) == std::array<int, 3>{ 255, 0, 0 }));
    assert((stamina_colour(100, 100, true) == std::array<int, 3>{ 0, 0, 255 }));
    using Colour = d2d::palette::Rgba;
    const std::array<Colour, 4> palette{{ { 255, 0, 0, 0 }, { 100, 90, 80, 255 },
                                         { 170, 20, 10, 255 }, { 220, 120, 100, 255 } }};
    const std::array<std::uint8_t, 4> indices{ 0, 1, 2, 3 };
    const auto red = notification_colour(std::span<const std::uint8_t>(indices), std::span<const Colour>(palette));
    assert((red == std::array<int, 3>{ 170, 20, 10 }));
    assert((notification_colour(std::span<const std::uint8_t>{}, std::span<const Colour>(palette)) == std::array<int, 3>{}));
    constexpr int width = 32, height = 32;
    std::vector<std::uint8_t> normal(width * height * 4, 220);
    draw_button_face(normal, width, height, 4, 4, 24, 24);
    const auto pixel = [](int x, int y) { return std::size_t((y * width + x) * 4); };
    assert(normal[pixel(4, 4)] == 220); // corners remain untouched
    assert(normal[pixel(16, 16)] < 50); // dark face contrasts with bright scenery
    assert(normal[pixel(16, 6)] > normal[pixel(16, 16)]); // bronze bevel
    auto notified = normal;
    tint_notification_face(notified, width, height, 4, 4, 24, 24, red);
    assert(notified[pixel(16, 16)] > notified[pixel(16, 16) + 1]);
    assert(notified[pixel(16, 6)] > notified[pixel(16, 16)]);
    assert(notified[pixel(4, 4)] == normal[pixel(4, 4)]);
    auto pressed = normal;
    shade_button_art(pressed, width, height, 4, 4, 24, 24, ButtonState::Pressed);
    assert(pressed[pixel(16, 16)] < normal[pixel(16, 16)]);
    assert(pressed[pixel(16, 6)] == normal[pixel(16, 6)]); // rim is preserved
    auto disabled = std::move(normal);   // its last use
    shade_button_art(disabled, width, height, 4, 4, 24, 24, ButtonState::Disabled);
    assert(disabled[pixel(16, 16)] < pressed[pixel(16, 16)]);
    std::vector<std::uint8_t> clipped(width * height * 4, 220);
    draw_button_face(clipped, width, height, -12, -12, 24, 24);
    assert(clipped[pixel(0, 0)] < 50);
    assert(clipped[pixel(31, 31)] == 220);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
