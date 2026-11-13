// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A path's blocked end walked back toward its start (FUN_00648120, and a player's push-on FUN_00648050): walk_back_cases.inc
// is game.exe's own run on random ends and walls (tools/emu/walk_back.py); rules::end_walk_back gives the same return and end.

#include <monsters.hpp>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <iterator>
#include <utility>
#include <vector>

namespace {

struct Case {
    int x, y, to_x, to_y, player;
    std::vector<std::pair<int, int>> walls;
    int found, end_x, end_y;
};
const Case kCases[] = {
#include "walk_back_cases.inc"
};

}   // namespace

int main() try {
    int bad = 0;
    for (const auto& one : kCases) {
        int to_x = one.to_x, to_y = one.to_y;
        const bool found = d2d::rules::end_walk_back(one.x, one.y, to_x, to_y, one.player != 0,
                                                     [&](int at_x, int at_y) { return std::ranges::contains(one.walls, std::pair{ at_x, at_y }); });
        if ((found != (one.found != 0) || to_x != one.end_x || to_y != one.end_y) && ++bad <= 5)
            std::printf("FAIL: end %d,%d player %d: d2d %d %d,%d, game.exe %d %d,%d\n", one.to_x, one.to_y, one.player, int(found), to_x, to_y, one.found,
                        one.end_x, one.end_y);
    }
    if (bad) { std::printf("%d of %zu differ\n", bad, std::size(kCases)); return 1; }
    std::printf("OK: %zu blocked ends walk back as FUN_00648120 walks them\n", std::size(kCases));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_walk_back: %s\n", error.what());
    return 1;
}
