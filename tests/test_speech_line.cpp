// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: dcc font game
// test-includes: apps/d2d
// A speech line that meets the box is drawn for the glyph rows inside it
// (FUN_00501df0). A line clear of the box is not drawn.
#include "ui.hpp"

#include <dc6.hpp>
#include <font.hpp>
#include <palette.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

namespace {

d2d::font::Font tall_glyph() {
    // One solid 1x3 glyph. Same fixture as test_text_box's baseline pixels.
    std::vector<std::byte> sprite(28 + 32 + 9);
    auto write32 = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned int index = 0; index < 4; ++index)
            sprite[offset + index] = std::byte((value >> (8 * index)) & 255);
    };
    write32(0, 6); write32(16, 1); write32(20, 1); write32(24, 28);
    write32(32, 1); write32(36, 3); write32(40, 2); write32(44, 0xffffffff);
    write32(56, 9);
    for (int index = 0; index < 3; ++index) {
        sprite[std::size_t(60 + 3 * index)] = std::byte{1};
        sprite[std::size_t(61 + 3 * index)] = std::byte{1};
        sprite[std::size_t(62 + 3 * index)] = std::byte{0x80};
    }
    std::vector<std::byte> metrics(26);
    metrics[0] = std::byte{'W'}; metrics[1] = std::byte{'o'};
    metrics[2] = std::byte{'o'}; metrics[3] = std::byte{'!'}; metrics[4] = std::byte{1};
    metrics[10] = std::byte{2}; metrics[12] = std::byte{'A'};
    metrics[15] = std::byte{2}; metrics[16] = std::byte{2};
    return d2d::font::Font(metrics, d2d::dc6::Sprite(sprite));
}

}  // namespace

int main() try {
    using d2d::client::kSpeechBoxBottom;
    using d2d::client::kSpeechBoxTop;
    using d2d::client::speech_line_visible;
    constexpr int cell = 16;
    assert(speech_line_visible(kSpeechBoxTop, cell));                 // baseline on the box's top row
    assert(!speech_line_visible(kSpeechBoxTop - 1, cell));            // the glyph ends above the box
    assert(speech_line_visible(kSpeechBoxBottom - 1, cell));          // baseline on the box's last row
    assert(speech_line_visible(kSpeechBoxBottom + cell - 2, cell));   // only the glyph's top row remains
    assert(!speech_line_visible(kSpeechBoxBottom + cell - 1, cell));  // the glyph starts on the row below the box

    const d2d::font::Font font = tall_glyph();
    std::array<d2d::palette::Rgba, 256> colours{};
    colours[1] = { 255, 255, 255, 255 };
    const d2d::palette::Palette palette(colours);
    constexpr int width = 8;
    const int height = kSpeechBoxTop + 4;
    std::vector<std::uint8_t> framebuffer(std::size_t(width * height) * 4);
    const int glyph_top = kSpeechBoxTop - 1;
    font.draw_tinted(framebuffer, width, std::uint32_t(height), palette, 0, glyph_top, "A", 255, 255, 255,
                     kSpeechBoxTop, kSpeechBoxBottom);
    for (int y = 0; y < height; ++y) {
        const bool inside = y >= kSpeechBoxTop && y < glyph_top + 3;
        assert((framebuffer[std::size_t(y * width * 4 + 3)] != 0) == inside);
    }
    std::printf("OK: a speech line keeps the glyph rows inside the box\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_speech_line: %s\n", error.what());
    return 1;
}
