// SPDX-License-Identifier: GPL-3.0-or-later
// The case lines of tools/emu/gear_absorb.py: `id life max mdr res x 3, absorb percent x 4, flat x 4 (fire, lightning, cold, magic),
// Cyclone pool, blows` (see its docstring), shared by test_gear_absorb and test_gear_absorb_rules.
#pragma once

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
    out.blows.clear();
    for (long blow = 0; blow < count; ++blow) {
        long fields = 0;
        if (!(words >> fields)) return false;
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

}   // namespace gear_absorb
