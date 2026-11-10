// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The ambient's dip to black (rules/ambient_dip.hpp): FUN_0046ae50 starts
// it, FUN_0046ad10 steps it a frame at a time, FUN_00474550 (flag 1) fills
// with it, against stock 1.14d's, run by tools/emu/ambient_dip.py: 300
// cases. No game data needed at runtime. With a cases file argument
// (ambient_dip.py's `gen` output) it prints its `game` lines instead, to diff.
#include <ambient_dip.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>

namespace {

// One case line: the dip's frames and its level's fill; the client's level
// changes at frame `change` to one filled with `other`. The fill after
// each frame (the dip's while it's lit, else the level's), until it's off.
std::string answer(const std::string& line) {
    std::istringstream fields(line);
    int seq = 0, fade_in = 0, hold = 0, back = 0, fill = 0, change = 0, other = 0;
    fields >> seq >> fade_in >> hold >> back >> fill >> change >> other;
    constexpr int kOwn = 1, kNext = 2;                              // the levels (not 8, 107, 108)
    d2d::rules::AmbientDip dip;
    dip.start(fade_in, hold, back, kOwn);
    std::string out;
    int client = kOwn;
    for (int frame = 0; dip.running; ++frame) {
        if (frame == change) client = kNext;
        const int level_fill = client == kOwn ? fill : other;
        dip.step(level_fill, client);
        const int light = dip.light(client);
        out += ' ' + std::to_string(light >= 0 ? light : level_fill);
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) {
        std::ifstream input(argv[1]);
        for (std::string line; std::getline(input, line);) std::printf("%s =%s\n", line.c_str(), answer(line).c_str());
        return 0;
    }
    struct Case { const char* line; const char* lights; };
    const Case cases[] = {
#include "ambient_dip_cases.inc"
    };
    for (const auto& row : cases) {
        const auto lights = answer(row.line);
        if (lights.substr(1) != row.lights) std::fprintf(stderr, "FAIL: %s:\n  d2d     %s\n  game.exe %s\n", row.line, lights.c_str() + 1, row.lights);
        assert(lights.substr(1) == row.lights);
    }
    // Who starts one: Cloak of Shadows at level 1 (auralen 200) and the
    // Cairn Stones (cairnstones: Param1 40, Range 300).
    static_assert(d2d::rules::cloak_dip(200).fade_in == 5 && d2d::rules::cloak_dip(200).hold == 120 && d2d::rules::cloak_dip(200).back == 80);
    static_assert(d2d::rules::cairn_dip(40, 300).fade_in == 40 && d2d::rules::cairn_dip(40, 300).hold == 220 && d2d::rules::cairn_dip(40, 300).back == 40);
    // The Den, River of Flame and the Chaos Sanctuary keep their own fill.
    d2d::rules::AmbientDip dip;
    dip.start(5, 10, 5, 1);
    dip.step(200, 1);
    assert(dip.light(1) == 200 && dip.light(8) == -1 && dip.light(107) == -1 && dip.light(108) == -1);
    std::printf("OK: %zu dips as game.exe's\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_ambient_dip: %s\n", error.what());
    return 1;
}
