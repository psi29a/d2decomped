// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The hire window's list box scroll bar (rules/hire_list.hpp): the rows
// wholly shown and the thumb (FUN_004be110, its cels), a release on the
// track (FUN_004bf2f0), a step (FUN_004be910 / FUN_004be990) and a wheel
// notch (FUN_004be860) against stock 1.14d's, run by
// tools/emu/hire_list_scroll.py: 2000 cases. No game data needed at runtime.
// With a cases file argument (its `gen` output) it prints its `game` lines
// instead, to diff.
#include <hire_list.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// One case line: the draw's FUN_004be110 first, then the input.
std::string answer(const std::string& line) {
    std::istringstream fields(line);
    int seq = 0, y = 0, height = 0, gap = 0, scroll_x = 0, first = 0, selected = 0, input = 0, arg = 0, rows = 0;
    fields >> seq >> y >> height >> gap >> scroll_x >> first >> selected >> input >> arg >> rows;
    std::vector<int> lines(std::size_t(rows > 0 ? rows : 0));
    for (auto& count : lines) fields >> count;
    const d2d::rules::ListBox box{ 0, y, scroll_x + 0x12, height, gap };
    const int whole = d2d::rules::list_rows_whole(box, lines, first);
    const auto thumb = d2d::rules::list_thumb(box, lines, first);
    std::string cels;
    if (thumb.shown)
        d2d::rules::list_thumb_cels(box, thumb, [&](d2d::rules::ListCel cel) {
            cels += ' ' + std::to_string(cel.frame) + ':' + std::to_string(scroll_x - 1) + ':' + std::to_string(cel.bottom);
        });
    d2d::rules::ListScroll scroll{ first, selected };
    if (input == 1) (void)d2d::rules::list_page(scroll, whole, rows, thumb, d2d::rules::list_scroll_y(box), arg);
    else if (input == 2 || input == 3) d2d::rules::list_step(scroll, whole, rows, input == 3);
    else if (input == 4) d2d::rules::list_wheel(scroll, whole, rows, arg < 0);
    return std::to_string(whole) + ' ' + std::to_string(thumb.top) + ' ' + std::to_string(thumb.bottom) + ' ' + std::to_string(scroll.first) + ' ' +
           std::to_string(scroll.selected) + cels;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) {
        std::ifstream input(argv[1]);
        for (std::string line; std::getline(input, line);) std::printf("%s = %s\n", line.c_str(), answer(line).c_str());
        return 0;
    }
    struct Case { const char* line; const char* answer; };
    const Case cases[] = {
#include "hire_list_scroll_cases.inc"
    };
    for (const auto& row : cases) {
        const auto got = answer(row.line);
        if (got != row.answer) std::fprintf(stderr, "FAIL: %s\n  d2d      %s\n  game.exe %s\n", row.line, got.c_str(), row.answer);
        assert(got == row.answer);
    }
    static_assert(d2d::rules::list_track_cells({ 0, 140, 0x1ea, 0x118 }) == 21 && d2d::rules::list_track_cells({ 0, 0, 0, 0x1f }) == 1);
    int count = 0, steps = 0;
    for (int draw = 0; draw < 30; ++draw) steps += d2d::rules::list_held_step(count) ? 1 : 0;
    assert(steps == 27);                                                   // 30 draws held: every one but the 10th, 20th, 30th
    std::printf("OK: %zu scroll bar cases as game.exe's\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_hire_list_scroll: %s\n", error.what());
    return 1;
}
