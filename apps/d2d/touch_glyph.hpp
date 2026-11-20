// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace d2d::client::touch {

enum class Glyph { Inventory, Skills, Quest, Bind, Map, More, Hand, Talk, Back, Character, Belt };

struct GlyphPoint {
    float x = 0.f;
    float y = 0.f;
    friend bool operator==(const GlyphPoint&, const GlyphPoint&) = default;
};

struct GlyphSegment {
    GlyphPoint from;
    GlyphPoint to;
    friend bool operator==(const GlyphSegment&, const GlyphSegment&) = default;
};

struct GlyphShape {
    std::vector<GlyphSegment> strokes;
    std::vector<GlyphPoint> dots;
};

/** Return thin-line pictogram geometry in [0, 1] button-local coordinates. */
inline GlyphShape glyph_shape(Glyph glyph) {
    using P = GlyphPoint;
    GlyphShape shape;
    const auto line = [&](P from, P to) { shape.strokes.push_back({ from, to }); };
    switch (glyph) {
    case Glyph::Inventory:
        // Handled bag: folded top, sides, base, and clasp.
        line({ .29f, .34f }, { .71f, .34f }); line({ .29f, .34f }, { .25f, .76f });
        line({ .71f, .34f }, { .75f, .76f }); line({ .25f, .76f }, { .75f, .76f });
        line({ .29f, .34f }, { .39f, .23f }); line({ .39f, .23f }, { .61f, .23f });
        line({ .61f, .23f }, { .71f, .34f }); line({ .44f, .44f }, { .56f, .44f });
        break;
    case Glyph::Skills:
        // Open book with a five-point star on the right page.
        line({ .50f, .30f }, { .50f, .75f }); line({ .50f, .38f }, { .34f, .30f });
        line({ .34f, .30f }, { .24f, .34f }); line({ .24f, .34f }, { .24f, .70f });
        line({ .24f, .70f }, { .50f, .76f }); line({ .50f, .38f }, { .66f, .30f });
        line({ .66f, .30f }, { .76f, .34f }); line({ .76f, .34f }, { .76f, .70f });
        line({ .76f, .70f }, { .50f, .76f });
        line({ .64f, .42f }, { .67f, .49f }); line({ .67f, .49f }, { .74f, .50f });
        line({ .74f, .50f }, { .68f, .55f }); line({ .68f, .55f }, { .70f, .62f });
        line({ .70f, .62f }, { .64f, .58f }); line({ .64f, .58f }, { .58f, .62f });
        line({ .58f, .62f }, { .60f, .55f }); line({ .60f, .55f }, { .54f, .50f });
        line({ .54f, .50f }, { .61f, .49f }); line({ .61f, .49f }, { .64f, .42f });
        break;
    case Glyph::Quest:
        // Rolled scroll with a short quest line.
        line({ .34f, .25f }, { .70f, .25f }); line({ .70f, .25f }, { .70f, .68f });
        line({ .70f, .68f }, { .34f, .68f }); line({ .34f, .68f }, { .34f, .25f });
        line({ .29f, .25f }, { .39f, .25f }); line({ .29f, .25f }, { .29f, .34f });
        line({ .29f, .34f }, { .39f, .34f }); line({ .39f, .34f }, { .39f, .25f });
        line({ .29f, .60f }, { .39f, .60f }); line({ .29f, .60f }, { .29f, .69f });
        line({ .29f, .69f }, { .39f, .69f }); line({ .39f, .69f }, { .39f, .60f });
        line({ .45f, .40f }, { .62f, .40f }); line({ .45f, .50f }, { .62f, .50f });
        break;
    case Glyph::Bind:
        // Two interlocking links, drawn as angular open loops.
        line({ .42f, .37f }, { .34f, .29f }); line({ .34f, .29f }, { .25f, .29f });
        line({ .25f, .29f }, { .18f, .36f }); line({ .18f, .36f }, { .18f, .45f });
        line({ .18f, .45f }, { .30f, .57f }); line({ .30f, .57f }, { .39f, .57f });
        line({ .39f, .57f }, { .46f, .50f }); line({ .46f, .50f }, { .50f, .46f });
        line({ .58f, .63f }, { .66f, .71f }); line({ .66f, .71f }, { .75f, .71f });
        line({ .75f, .71f }, { .82f, .64f }); line({ .82f, .64f }, { .82f, .55f });
        line({ .82f, .55f }, { .70f, .43f }); line({ .70f, .43f }, { .61f, .43f });
        line({ .61f, .43f }, { .54f, .50f }); line({ .54f, .50f }, { .50f, .54f });
        line({ .38f, .62f }, { .62f, .38f });
        break;
    case Glyph::Map:
        // Folded three-panel map with a marked destination.
        line({ .23f, .31f }, { .42f, .25f }); line({ .42f, .25f }, { .59f, .32f });
        line({ .59f, .32f }, { .77f, .25f }); line({ .23f, .31f }, { .23f, .70f });
        line({ .23f, .70f }, { .42f, .76f }); line({ .42f, .76f }, { .59f, .69f });
        line({ .59f, .69f }, { .77f, .76f }); line({ .77f, .76f }, { .77f, .25f });
        line({ .42f, .25f }, { .42f, .76f }); line({ .59f, .32f }, { .59f, .69f });
        line({ .61f, .42f }, { .66f, .38f }); line({ .66f, .38f }, { .71f, .42f });
        line({ .71f, .42f }, { .71f, .48f }); line({ .71f, .48f }, { .66f, .55f });
        line({ .66f, .55f }, { .61f, .48f }); line({ .61f, .48f }, { .61f, .42f });
        break;
    case Glyph::Hand:
        line({ .35f, .72f }, { .24f, .51f }); line({ .24f, .51f }, { .30f, .47f });
        line({ .30f, .47f }, { .40f, .56f }); line({ .40f, .56f }, { .40f, .25f });
        line({ .40f, .25f }, { .48f, .25f }); line({ .48f, .25f }, { .48f, .47f });
        line({ .48f, .47f }, { .54f, .35f }); line({ .54f, .35f }, { .62f, .37f });
        line({ .62f, .37f }, { .68f, .42f }); line({ .68f, .42f }, { .75f, .45f });
        line({ .75f, .45f }, { .70f, .70f }); line({ .70f, .70f }, { .35f, .72f });
        break;
    case Glyph::Talk:
        line({ .22f, .27f }, { .78f, .27f }); line({ .78f, .27f }, { .78f, .61f });
        line({ .78f, .61f }, { .49f, .61f }); line({ .49f, .61f }, { .34f, .74f });
        line({ .34f, .74f }, { .36f, .61f }); line({ .36f, .61f }, { .22f, .61f });
        line({ .22f, .61f }, { .22f, .27f });
        shape.dots = { { .36f, .44f }, { .50f, .44f }, { .64f, .44f } };
        break;
    case Glyph::Character:
        // Head and shoulders of a character portrait.
        line({ .42f, .25f }, { .58f, .25f }); line({ .58f, .25f }, { .64f, .32f });
        line({ .64f, .32f }, { .64f, .44f }); line({ .64f, .44f }, { .58f, .51f });
        line({ .58f, .51f }, { .42f, .51f }); line({ .42f, .51f }, { .36f, .44f });
        line({ .36f, .44f }, { .36f, .32f }); line({ .36f, .32f }, { .42f, .25f });
        line({ .42f, .51f }, { .28f, .58f }); line({ .28f, .58f }, { .24f, .73f });
        line({ .24f, .73f }, { .76f, .73f }); line({ .76f, .73f }, { .72f, .58f });
        line({ .72f, .58f }, { .58f, .51f });
        break;
    case Glyph::Back:
        // A curved return arrow, with its point facing left.
        line({ .42f, .29f }, { .24f, .47f }); line({ .24f, .47f }, { .42f, .65f });
        line({ .26f, .47f }, { .52f, .47f }); line({ .52f, .47f }, { .64f, .50f });
        line({ .64f, .50f }, { .71f, .58f }); line({ .71f, .58f }, { .71f, .67f });
        line({ .71f, .67f }, { .65f, .74f }); line({ .65f, .74f }, { .54f, .77f });
        break;
    case Glyph::More:
        shape.dots = { { .29f, .50f }, { .50f, .50f }, { .71f, .50f } };
        break;
    case Glyph::Belt:
        // Four potion slots in a compact two-by-two belt.
        line({ .27f, .27f }, { .45f, .27f }); line({ .45f, .27f }, { .45f, .45f });
        line({ .45f, .45f }, { .27f, .45f }); line({ .27f, .45f }, { .27f, .27f });
        line({ .55f, .27f }, { .73f, .27f }); line({ .73f, .27f }, { .73f, .45f });
        line({ .73f, .45f }, { .55f, .45f }); line({ .55f, .45f }, { .55f, .27f });
        line({ .27f, .55f }, { .45f, .55f }); line({ .45f, .55f }, { .45f, .73f });
        line({ .45f, .73f }, { .27f, .73f }); line({ .27f, .73f }, { .27f, .55f });
        line({ .55f, .55f }, { .73f, .55f }); line({ .73f, .55f }, { .73f, .73f });
        line({ .73f, .73f }, { .55f, .73f }); line({ .55f, .73f }, { .55f, .55f });
        break;
    }
    return shape;
}

// Render a cached pictogram in the upper part of a control, leaving room for its caption.
inline void draw_glyph(std::vector<std::uint8_t>& pixels, int width, int height,
                       float x, float y, float box_width, float box_height, Glyph glyph, bool active = false) {
    if (width <= 0 || height <= 0 || box_width <= 0 || box_height <= 0
        || pixels.size() < std::size_t(width) * std::size_t(height) * 4) return;
    static const std::array shapes{ glyph_shape(Glyph::Inventory), glyph_shape(Glyph::Skills),
        glyph_shape(Glyph::Quest), glyph_shape(Glyph::Bind), glyph_shape(Glyph::Map),
        glyph_shape(Glyph::More), glyph_shape(Glyph::Hand), glyph_shape(Glyph::Talk),
        glyph_shape(Glyph::Back), glyph_shape(Glyph::Character), glyph_shape(Glyph::Belt) };
    const auto& shape = shapes[std::size_t(glyph)];
    const float size = std::min(box_width, box_height) * .60f;
    const float left = x + (box_width - size) * .5f, top = y + 1.f;
    const float radius = std::min(box_width, box_height) * .5f - 2.f;
    const auto pixel = [&](int column, int row) {
        if (column < 0 || column >= width || row < 0 || row >= height
            || std::hypot(float(column) + .5f - x - box_width * .5f,
                          float(row) + .5f - y - box_height * .5f) >= radius) return;
        auto* output = &pixels[(std::size_t(row) * std::size_t(width) + std::size_t(column)) * 4];
        output[0] = active ? 255 : 199; output[1] = active ? 220 : 179; output[2] = 119;
    };
    for (const auto& stroke : shape.strokes) {
        const float from_x = left + stroke.from.x * size, from_y = top + stroke.from.y * size;
        const float to_x = left + stroke.to.x * size, to_y = top + stroke.to.y * size;
        const int steps = std::max(1, int(std::ceil(std::hypot(to_x - from_x, to_y - from_y) * 2.f)));
        for (int step = 0; step <= steps; ++step) {
            const float fraction = float(step) / float(steps);
            pixel(int(std::lround(from_x + (to_x - from_x) * fraction)),
                  int(std::lround(from_y + (to_y - from_y) * fraction)));
        }
    }
    for (const auto& dot : shape.dots) {
        const int column = int(std::lround(left + dot.x * size)), row = int(std::lround(top + dot.y * size));
        for (int offset = -1; offset <= 1; ++offset) { pixel(column + offset, row); pixel(column, row + offset); }
    }
}

} // namespace d2d::client::touch
