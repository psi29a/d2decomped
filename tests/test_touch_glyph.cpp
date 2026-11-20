// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
#include "touch_glyph.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

int main() try {
    using namespace d2d::client::touch;
    constexpr std::array glyphs{ Glyph::Inventory, Glyph::Skills, Glyph::Quest,
                                 Glyph::Bind, Glyph::Map, Glyph::More, Glyph::Hand, Glyph::Talk,
                                 Glyph::Back, Glyph::Character, Glyph::Belt };
    std::array<GlyphShape, glyphs.size()> shapes;
    for (std::size_t index = 0; index < glyphs.size(); ++index) {
        shapes[index] = glyph_shape(glyphs[index]);
        const auto& shape = shapes[index];
        assert(!shape.strokes.empty() || !shape.dots.empty());
        for (const auto& segment : shape.strokes) {
            for (const auto point : { segment.from, segment.to }) {
                assert(point.x >= 0.f && point.x <= 1.f);
                assert(point.y >= 0.f && point.y <= 1.f);
            }
        }
        for (const auto point : shape.dots) {
            assert(point.x >= 0.f && point.x <= 1.f);
            assert(point.y >= 0.f && point.y <= 1.f);
        }
        for (std::size_t prior = 0; prior < index; ++prior)
            assert(shape.strokes != shapes[prior].strokes || shape.dots != shapes[prior].dots);
    }
    assert(shapes[0].strokes.size() == 8);  // inventory bag
    assert(shapes[1].strokes.size() == 19); // book and star
    assert(shapes[2].strokes.size() == 14); // scroll
    assert(shapes[3].strokes.size() == 17); // chain links
    assert(shapes[4].strokes.size() == 16); // folded map
    assert(shapes[5].strokes.empty() && shapes[5].dots.size() == 3);
    assert(shapes[10].strokes.size() == 16); // four belt slots
    std::vector<std::uint8_t> image(32 * 32 * 4, 17);
    draw_glyph(image, 32, 32, -8, -8, 48, 48, Glyph::Inventory);
    bool drawn = false;
    for (std::size_t index = 0; index < image.size(); index += 4) {
        drawn = drawn || image[index] != 17;
        assert(image[index + 3] == 17); // Overlay preserves alpha.
    }
    assert(drawn);
    const auto clipped = image;
    draw_glyph(image, 32, 32, 64, 64, 48, 48, Glyph::Map);
    assert(image == clipped);
    std::vector<std::uint8_t> short_buffer(3, 17);
    draw_glyph(short_buffer, 32, 32, 0, 0, 48, 48, Glyph::Map);
    assert(short_buffer == std::vector<std::uint8_t>(3, 17));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_touch_glyph: %s\n", error.what());
    return 1;
}
