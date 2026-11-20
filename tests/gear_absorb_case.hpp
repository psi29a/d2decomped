// SPDX-License-Identifier: GPL-3.0-or-later
// The case lines of tools/emu/gear_absorb.py: `id life max mdr res x 3, absorb percent x 4, flat x 4 (fire, lightning, cold, magic),
// Cyclone pool, blows` (see its docstring), shared by test_gear_absorb and test_gear_absorb_rules.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace gear_absorb {

struct Blow { std::vector<std::pair<int, int>> fields; };   // (element type, whole points)
struct Case {
    long id = 0, life = 0, max_life = 0, mdr = 0, pool = 0;
    std::array<long, 3> res{};
    std::array<long, 4> pct{}, flat{};   // fire, lightning, cold, magic
    std::vector<Blow> blows;
};

inline bool parse(const std::string& line, Case& out) {
    std::istringstream words(line);
    long count = 0;
    if (!(words >> out.id >> out.life >> out.max_life >> out.mdr >> out.res[0] >> out.res[1] >> out.res[2])) return false;
    for (auto& value : out.pct) words >> value;
    for (auto& value : out.flat) words >> value;
    if (!(words >> out.pool >> count)) return false;
    // Bounded so a malformed line can't run a loop away: gear_absorb.py writes 1 to 3 blows of 1 to 3 fields.
    count = std::clamp(count, 0L, 64L);
    out.blows.clear();
    for (long blow = 0; blow < count; ++blow) {
        long fields = 0;
        if (!(words >> fields)) return false;
        fields = std::clamp(fields, 0L, 16L);
        Blow one;
        for (long k = 0; k < fields; ++k) {
            int type = 0, points = 0;
            if (!(words >> type >> points)) return false;
            one.fields.emplace_back(type, points);
        }
        out.blows.push_back(std::move(one));
    }
    return true;
}

// game.exe's lives (gear_absorb.py game on these lines; V run): the heal capped at max life then the total off (id 0: 90 of 100 life,
// 20 % cold absorb, a 100-point cold blow: 90 + 20 -> 100, minus 80 = 20 points); a Cyclone Armor pool of 30 points first (1);
// below max life, the heal not capped (2); a heal that does not save the player (3); two blows in a frame, the cap taken per blow (4: 30, not 20);
// a flat absorb (5); the order resistance, percent, flat, with the heal (6); a negative field heals nothing (7); three fields at once (8);
// the percent stopping at 40, the heal capped at max life (9); a life above max is brought down to it by a heal (10: 120 -> 100, minus 40)
// and kept without one (11: 110 minus 10); a blow the flat absorb takes whole heals and subtracts nothing (12: 1 point -> 11);
// a blow after a killing one heals nothing (13: the oracle stops at the death, as FUN_0057c6c0 skips a dead unit).
inline const std::vector<std::pair<std::string, std::int64_t>> kPins = {
    { "0 23040 100 0 0 0 0 0 0 20 0 0 0 0 0 0 1 1 2 100", 5120 },
    { "1 23040 100 0 0 0 0 0 0 20 0 0 0 0 0 7680 1 1 2 100", 11264 },
    { "2 12800 100 0 0 0 0 0 0 20 0 0 0 0 0 0 1 1 2 60", 3584 },
    { "3 12800 100 0 0 0 0 0 0 20 0 0 0 0 0 0 1 1 2 100", 0 },
    { "4 25600 100 0 0 0 0 0 0 20 0 0 0 0 0 0 2 1 2 50 1 2 50", 7680 },
    { "5 15360 100 0 0 0 0 0 0 0 0 0 0 10 0 0 1 1 2 40", 10240 },
    { "6 15360 100 0 50 0 0 30 0 0 0 20 0 0 0 0 1 1 0 100", 20480 },
    { "7 10240 100 3 0 0 0 0 0 0 36 0 0 0 0 0 1 1 4 2", 10240 },
    { "8 17920 100 0 0 0 0 40 40 40 40 5 5 5 5 0 1 3 0 30 1 30 2 30", 15616 },
    { "9 7680 50 0 0 0 0 0 0 60 0 0 0 0 0 0 1 1 2 100", 0 },
    { "10 30720 100 0 0 0 0 0 0 20 0 0 0 0 0 0 1 1 2 50", 15360 },
    { "11 28160 100 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 2 10", 25600 },
    { "12 256 100 0 0 0 0 0 0 0 0 0 0 20 0 0 1 1 2 10", 2816 },
    { "13 2600 100 0 0 0 0 0 0 0 0 0 0 20 0 0 2 1 0 10 1 2 10", 0 },
};

}   // namespace gear_absorb
