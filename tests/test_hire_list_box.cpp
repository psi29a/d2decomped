// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The hire window's list box (rules/hire_list.hpp): the rows that fit
// (FUN_004be340) and the row under the mouse (FUN_004bea50) against stock
// 1.14d's, run by tools/emu/hire_list_box.py: 2000 cases. No game data
// needed at runtime. With a cases file argument (hire_list_box.py's `gen`
// output) it prints its `game` lines instead, to diff.
#include <hire_list.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Answer { int shown = 0, row_at = -1; };

// One case line: the box at x 0 (the scroll bar's x gives its width), its
// rows' line counts, the first shown and the mouse.
Answer answer(const std::string& line) {
    std::istringstream fields(line);
    int seq = 0, y = 0, height = 0, gap = 0, scroll_x = 0, first = 0, mouse_x = 0, mouse_y = 0, rows = 0;
    fields >> seq >> y >> height >> gap >> scroll_x >> first >> mouse_x >> mouse_y >> rows;
    // Bounded so a malformed line can't run a loop away: the box's 128 rows (FUN_004bf650),
    // a screen's coordinates; no case is outside them.
    rows = std::clamp(rows, 0, 0x80);
    first = std::clamp(first, 0, 0x80);
    y = std::clamp(y, -0x800, 0x800); height = std::clamp(height, 0, 0x800); gap = std::clamp(gap, 0, 0x100);
    scroll_x = std::clamp(scroll_x, -0x800, 0x800);
    mouse_x = std::clamp(mouse_x, -0x800, 0x800); mouse_y = std::clamp(mouse_y, -0x800, 0x800);
    std::vector<int> lines(static_cast<std::size_t>(rows));
    for (auto& count : lines) fields >> count;
    const d2d::rules::ListBox box{ 0, y, scroll_x + 0x12, height, gap };
    return { d2d::rules::list_rows_shown(box, lines, first), d2d::rules::list_row_at(box, lines, first, mouse_x, mouse_y) };
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) {
        std::ifstream input(argv[1]);
        for (std::string line; std::getline(input, line);) {
            const auto [shown, row_at] = answer(line);
            std::printf("%s = %d %d\n", line.c_str(), shown, row_at);
        }
        return 0;
    }
    struct Case { const char* line; int shown, row_at; };
    const Case cases[] = {
#include "hire_list_box_cases.inc"
    };
    for (const auto& row : cases) {
        const auto [shown, row_at] = answer(row.line);
        if (shown != row.shown || row_at != row.row_at)
            std::fprintf(stderr, "FAIL: %s: d2d %d %d, game.exe %d %d\n", row.line, shown, row_at, row.shown, row.row_at);
        assert(shown == row.shown && row_at == row.row_at);
    }
    // The hire window's box at 800 x 600, two-line rows: seven fit, not eight.
    const auto box = d2d::rules::hire_list_box(800, 600);
    static_assert(d2d::rules::hire_list_box(800, 600).x == 155 && d2d::rules::hire_list_box(800, 600).y == 140 && d2d::rules::hire_window_y(600) == 105);
    const std::vector<int> ten(10, 2);
    assert(d2d::rules::list_rows_shown(box, ten, 0) == 7 && d2d::rules::list_rows_shown(box, ten, 2) == 7);
    assert(d2d::rules::list_rows_shown(box, ten, 3) == 10);                 // the rest all fit: the whole count
    std::printf("OK: %zu list box cases as game.exe's; the hire box shows 7 rows\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_hire_list_box: %s\n", error.what());
    return 1;
}
