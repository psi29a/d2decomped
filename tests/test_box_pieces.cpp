// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A menu box's frame (rules/box_pieces.hpp): the boxpieces cels FUN_00452e50
// draws round a rectangle, against stock 1.14d's, run by
// tools/emu/box_pieces.py: 2000 cases. No game data needed at runtime. With a
// cases file argument (box_pieces.py's `gen` output) it prints its `game`
// lines instead, to diff.
#include <box_pieces.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>

namespace {

// One case line: `seq left top last_x last_y`, answered with each cel as frame:x:y.
std::string answer(const std::string& line) {
    std::istringstream fields(line);
    int seq = 0, left = 0, top = 0, last_x = 0, last_y = 0;
    fields >> seq >> left >> top >> last_x >> last_y;
    // Bounded so a malformed line can't run a loop away; no case is outside a screen's span.
    left = std::clamp(left, -0x1000, 0x1000); top = std::clamp(top, -0x1000, 0x1000);
    last_x = std::clamp(last_x, -0x1000, 0x1000); last_y = std::clamp(last_y, -0x1000, 0x1000);
    std::string cels;
    d2d::rules::box_piece_cels(left, top, last_x, last_y, [&](d2d::rules::BoxCel cel) {
        if (!cels.empty()) cels += ' ';
        cels += std::to_string(cel.frame) + ':' + std::to_string(cel.left) + ':' + std::to_string(cel.bottom);
    });
    return cels;
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
#include "box_pieces_cases.inc"
    };
    int failed = 0;
    for (const auto& row : cases) {
        const auto got = answer(row.line);
        if (got == row.answer) continue;
        std::fprintf(stderr, "FAIL: %s\n  d2d      %s\n  game.exe %s\n", row.line, got.c_str(), row.answer);
        ++failed;
    }
    assert(failed == 0);
    if (failed) return 1;
    std::printf("OK: %zu box frames as game.exe's\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_box_pieces: %s\n", error.what());
    return 1;
}
