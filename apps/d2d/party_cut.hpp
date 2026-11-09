// SPDX-License-Identifier: GPL-3.0-or-later
// The Party screen's two text cuts (docs/research/re/party.md), each on a
// `measure(text) -> px` (FUN_00501820 in the font set).
#pragma once

#include <string>

namespace d2d::client {

// FUN_0049a2c0: a row's name, cut at 0x60 px for a player in a party, 0x88
// in none (FUN_0049c270: FUN_00479bc0 != -1). Under the limit it's kept;
// else letters go from its end while it measures the limit or more, and
// "..." goes on unmeasured, so the name can pass the limit by the dots.
template<class Measure>
std::string cut_party_name(std::string name, bool in_party, Measure measure) {
    const int limit = in_party ? 0x60 : 0x88;
    if (measure(name) < limit) return name;
    while (!name.empty() && measure(name) >= limit) name.pop_back();
    return name + "...";
}

// FUN_0049a830: a member's area, past 0x8c px. The suffix is "..." and the
// area's last letter ("....", 0x6da05c, its fourth unit swapped at
// 0x49a8ce); letters go from its end while it measures more than 0x8b less
// the suffix, then the suffix goes on: "Catacombs Lev...4".
template<class Measure>
std::string cut_party_area(std::string area, Measure measure) {
    if (measure(area) <= 0x8c) return area;
    const std::string suffix = "..." + area.substr(area.size() - 1);
    const int limit = 0x8b - measure(suffix);
    while (!area.empty() && measure(area) > limit) area.pop_back();
    return area + suffix;
}

}  // namespace d2d::client
