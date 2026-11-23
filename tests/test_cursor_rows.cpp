// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// test-includes: apps/d2d
// The hand cursor's rows (kCursorRows, row_loops, row_length) against
// stock 1.14d's table at 0x712010 and its getters FUN_00467e40 (+4, the
// row loops) and FUN_00467e90 (+8 << 8, the length in 8.8 frames), run by
// tools/emu/cursor_rows.py into cursor_rows_cases.inc.
#include <input/cursor_anim.hpp>

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <string_view>

namespace {

struct Row { int row; const char* name; int animated, loops, length, step, pressable; };
constexpr Row kGame[] = {
#include "cursor_rows_cases.inc"
};

}  // namespace

int main() try {
    static_assert(std::size(kGame) == d2d::client::kCursorRows.size());
    for (const auto& game : kGame) {
        const auto& ours = d2d::client::kCursorRows[std::size_t(game.row)];
        assert(std::string_view(ours.name) == game.name);
        assert(int(ours.animated) == game.animated && int(ours.pressable) == game.pressable && ours.step == game.step);
        assert(int(d2d::client::row_loops(game.row)) == game.loops);
        assert(d2d::client::row_length(game.row) == game.length);
    }
    std::puts("ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_cursor_rows: %s\n", error.what());
    return 1;
}
