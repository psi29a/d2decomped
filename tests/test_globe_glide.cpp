// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The HUD's shown life / mana / stamina (FUN_00496dd0, 0x496dd0) against
// stock 1.14d's own, run by tools/emu/globe_glide.py: 300 runs of calls,
// the three slots zeroed at each new run. No game data needed at runtime.
// With a file argument (globe_glide.py's `game` output, or its `gen`
// cases) it prints each line with d2d's shown value instead, to diff.
#include <hud_glide.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

namespace {

struct Run {
    int seq = -1;
    std::array<d2d::rules::Glide, 3> slots{};
    int shown(int seq_now, int stat, std::uint32_t frame, int cur, int max) {
        if (seq_now != seq) { seq = seq_now; slots = {}; }
        return d2d::rules::glide(slots[std::size_t((stat - 6) / 2)], stat == 6, cur, max, frame);
    }
};

}  // namespace

int main(int argc, char** argv) {
    Run run;
    if (argc > 1) {
        std::ifstream input(argv[1]);
        int seq = 0, stat = 0, cur = 0, max = 0;
        std::uint32_t frame = 0;
        std::string rest;
        while (input >> seq >> stat >> frame >> cur >> max) {
            std::getline(input, rest);
            std::printf("%d %d %u %d %d %d\n", seq, stat, frame, cur, max, run.shown(seq, stat, frame, cur, max));
        }
        return 0;
    }
    struct Case { int seq, stat; std::uint32_t frame; int cur, max, shown; };
    const Case cases[] = {
#include "globe_glide_cases.inc"
    };
    for (const auto& row : cases) {
        const int shown = run.shown(row.seq, row.stat, row.frame, row.cur, row.max);
        if (shown != row.shown) std::fprintf(stderr, "FAIL: seq %d stat %d frame %u cur %d max %d: d2d %d, game.exe %d\n", row.seq, row.stat, row.frame, row.cur, row.max, shown, row.shown);
        assert(shown == row.shown);
    }
    std::printf("OK: %zu glide calls as game.exe's\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
}
