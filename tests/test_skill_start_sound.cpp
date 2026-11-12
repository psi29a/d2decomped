// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A skill's start sounds (rules::sound_ids::skill_start) on a player or a
// monster, against stock 1.14d's FUN_004c6140 run by
// tools/emu/skill_start_sound.py: 300 cases. No game data needed at
// runtime. With a cases file argument (skill_start_sound.py's `gen`
// output) it prints its `game` lines instead, to diff.
#include <sound_ids.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>

namespace {

// One case line (skill_start_sound.py's docstring): what it plays, in order.
std::string answer(const std::string& line) {
    std::istringstream fields(line);
    int unit_type = 0, unit_class = 0, char_class = 0, flags5 = 0, flags7 = 0, st_sound = 0, st_class = 0, item_sound = 0, from_item = 0, cltst = 0, hit_class = 0, rate = 0;
    fields >> unit_type >> unit_class >> char_class >> flags5 >> flags7 >> st_sound >> st_class >> item_sound >> from_item >> cltst >> hit_class >> rate;
    const d2d::rules::sound_ids::SkillStart skill{ .st_sound = st_sound, .st_sound_class = st_class, .item_cast_sound = item_sound,
                                                   .char_class = char_class == 255 ? -1 : char_class, .delay = (flags5 & 0x20) != 0, .weapon = (flags5 & 0x40) != 0 };
    const bool refused = (flags5 & 0x10) != 0 && cltst == 2;
    std::string out;
    for (const auto cue : d2d::rules::sound_ids::skill_start(skill, unit_type, unit_class, hit_class, rate, from_item != 0, refused))
        if (cue.sound != 0) out += ' ' + std::to_string(cue.sound) + ':' + std::to_string(cue.ticks);
    return out;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) {
        std::ifstream input(argv[1]);
        for (std::string line; std::getline(input, line);) std::printf("%s =%s\n", line.c_str(), answer(line).c_str());
        return 0;
    }
    struct Case { const char* line; const char* cues; };
    const Case cases[] = {
#include "skill_start_sound_cases.inc"
    };
    for (const auto& row : cases) {
        const auto cues = answer(row.line);
        const std::string got = cues.empty() ? cues : cues.substr(1);
        if (got != row.cues) std::fprintf(stderr, "FAIL: %s:\n  d2d      %s\n  game.exe %s\n", row.line, got.c_str(), row.cues);
        assert(got == row.cues);
    }
    // Duriel's Smite (Skills.txt: stsound weapon_2hs_large_1, row 280, and
    // stsounddelay) with his MonStats2 HitClass 3 (6 frames) at rate 256:
    // the swing's 6 ticks less 3.
    const d2d::rules::sound_ids::SkillStart smite{ .st_sound = 280, .delay = true };
    const auto duriel = d2d::rules::sound_ids::skill_start(smite, 1, 211, 3, 256, false, false);
    assert(duriel[0].sound == 280 && duriel[0].ticks == 3 && duriel[1].sound == 0 && duriel[2].sound == 0);
    std::printf("OK: %zu skill starts as game.exe's\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_skill_start_sound: %s\n", error.what());
    return 1;
}
