// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A charge-up's charge sound (rules::sound_ids::charge_up) against stock
// 1.14d's cltprgfunc 12 / 13 (FUN_004f30a0, FUN_004f31a0) run by
// tools/emu/charge_up_sound.py: 300 cases. No game data needed at runtime.
// With a cases file argument (charge_up_sound.py's `gen` output) it prints
// its `game` lines instead, to diff.
#include <sound_ids.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>

namespace {

// One case line (charge_up_sound.py's docstring): the sound it plays, empty for none.
std::string answer(const std::string& line) {
    std::istringstream fields(line);
    int function = 0, flags = 0, state = 0, stat = 0, listed = 0, count = 0, prg_sound = 0, target = 0;
    fields >> function >> flags >> state >> stat >> listed >> count >> prg_sound >> target;
    const auto sound = d2d::rules::sound_ids::charge_up({ .prg_sound = prg_sound, .progressive = (flags & 4) != 0, .state = state, .states = 200, .stat = stat,
                                                          .stats = 400, .listed = listed != 0, .count = count, .untargeted = function == 12 && target == 0 });
    return sound ? std::to_string(*sound) : std::string();
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) {
        std::ifstream input(argv[1]);
        for (std::string line; std::getline(input, line);) {
            const auto sound = answer(line);
            std::printf("%s =%s%s\n", line.c_str(), sound.empty() ? "" : " ", sound.c_str());
        }
        return 0;
    }
    struct Case { const char* line; const char* sound; };
    const Case cases[] = {
#include "charge_up_sound_cases.inc"
    };
    for (const auto& row : cases) {
        const auto got = answer(row.line);
        if (got != row.sound) std::fprintf(stderr, "FAIL: %s:\n  d2d      %s\n  game.exe %s\n", row.line, got.c_str(), row.sound);
        assert(got == row.sound);
    }
    // Tiger Strike (Skills.txt row 254: prgsound assassin_chargeup_tiger_1,
    // Sounds.txt row 2297) on its third charge: tiger_3, row 2299.
    assert(d2d::rules::sound_ids::charge_up({ .prg_sound = 2297, .progressive = true, .state = 122, .states = 200, .stat = 318, .stats = 400, .listed = true, .count = 3 }) == 2299);
    std::printf("OK: %zu charge-up sounds as game.exe's\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_charge_up_sound: %s\n", error.what());
    return 1;
}
