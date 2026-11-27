// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// test-sources: apps/d2d/input/touch/trade.cpp
#include "input/touch/input.hpp"
#include "input/touch/trade.hpp"

#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    using namespace d2d::client::touch;
    const Box legacy{ 96, 123, 290, 290 };
    for (const float width : { 800.f, 1066.f, 1400.f }) {
        viewport_width = width;
        const auto inside = trade_drop_preview(legacy, 100, 130);
        assert(inside.dropping && inside.inside);
        assert(inside.box.x == to_viewport_x(legacy.x) && inside.box.y == legacy.y);
        assert(inside.box.contains(to_viewport_x(100), 130));
        // A legacy miss may still fall inside the translated drawing box.
        assert(!trade_drop_preview(legacy, 400, 130).inside);
        assert(trade_drop_preview(legacy, legacy.x, legacy.y).inside);
        assert(!trade_drop_preview(legacy, legacy.x + legacy.width, legacy.y).inside);
        assert(!trade_drop_preview(legacy, legacy.x, legacy.y + legacy.height).inside);
        const auto empty = trade_drop_preview({}, 0, 0);
        assert(!empty.dropping && !empty.inside);
    }
    viewport_width = 800.f;
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
