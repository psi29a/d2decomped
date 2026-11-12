// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: palette
// DirectDraw's gamma ramp (FUN_006be190) and the Video Options slider
// truncation (FUN_0047cc90, FUN_0047cd00).
#include <palette.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

namespace {

d2d::palette::Palette ramp_palette() {
    std::array<d2d::palette::Rgba, 256> colours{};
    for (int index = 0; index < 256; ++index)
        colours[std::size_t(index)] = { std::uint8_t(index), std::uint8_t(index), std::uint8_t(index), std::uint8_t(index == 0 ? 0 : 0xFF) };
    return d2d::palette::Palette(colours);
}

void expect_contrast_100(const d2d::palette::Palette& palette, int gamma) {
    for (const int index : { 1, 16, 64, 128, 192, 255 }) {
        const int level = std::min(255, index * gamma / 100);
        assert(palette[std::uint8_t(index)].r == level);
        assert(palette[std::uint8_t(index)].g == level);
        assert(palette[std::uint8_t(index)].b == level);
        assert(palette.entries()[std::size_t(index)].r == index);
    }
}

}  // namespace

int main() try {
    using d2d::palette::Palette;
    assert(Palette::gamma() == 155);
    assert(Palette::contrast() == 100);

    const auto palette = ramp_palette();
    assert(palette[0].a == 0);
    assert(palette[1].a == 0xFF);
    expect_contrast_100(palette, 155);
    assert(palette.nearest(128, 128, 128) == 128);

    Palette::set_ramp(100, 100);
    assert(Palette::gamma() == 100);
    for (int index = 0; index < 256; ++index) assert(palette[std::uint8_t(index)].r == index);
    expect_contrast_100(palette, 100);

    Palette::set_ramp(55, 100);
    expect_contrast_100(palette, 55);
    Palette::set_ramp(255, 100);
    expect_contrast_100(palette, 255);

    // FUN_006be190's blend, truncated (FUN_00682fd0 with the SSE2 flag set).
    // The research note's samples are 128 -> 163 and 192 -> 212.
    Palette::set_ramp(155, 0);
    assert(palette[1].r == 7);
    assert(palette[16].r == 42);
    assert(palette[64].r == 104);
    assert(palette[128].r == 163);
    assert(palette[192].r == 212);
    assert(palette[255].r == 255);
    assert(palette.entries()[128].r == 128);

    Palette::set_ramp(155, 50);
    assert(palette[1].r == 4);
    assert(palette[16].r == 33);
    assert(palette[64].r == 101);
    assert(palette[128].r == 180);
    assert(palette[192].r == 233);
    assert(palette[255].r == 255);

    using d2d::palette::slider_position;
    using d2d::palette::slider_value;
    assert(slider_position(21, 55, 255, 155) == 10);
    assert(slider_value(21, 55, 255, 10) == 155);
    assert(slider_position(21, 55, 255, 55) == 0);
    assert(slider_value(21, 55, 255, 0) == 55);
    assert(slider_position(21, 55, 255, 255) == 20);
    assert(slider_value(21, 55, 255, 20) == 255);
    assert(slider_position(21, 55, 255, 100) == 4);
    assert(slider_position(100, 0, 100, 100) == 99);
    assert(slider_value(100, 0, 100, 99) == 99);
    assert(slider_position(100, 0, 100, 0) == 0);
    assert(slider_value(100, 0, 100, 0) == 0);
    assert(slider_position(100, 0, 100, 50) == 50);
    assert(slider_value(100, 0, 100, 50) == 50);

    std::printf("OK\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
