// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: drlg
// level_vis: act 5's outdoor levels each name the levels whose rects share
// an edge with theirs (FUN_00678a70). FUN_00677680 makes the Frigid
// Highlands' outdoor neighbours before it is linked to the Bloody Foothills,
// but once the act is made its list (FUN_0066c040) names 110 too: run on
// game.exe for map seed 0x1234, 111's Vis is {112, 110}, both Warp -1, and
// its preset rooms hold slot bits 0x30 (tools/emu/room_vis.py). Made-up
// tables; no game files.
#include <drlg.hpp>
#include <level_ids.hpp>
#include <outdoor_data.hpp>
#include <txt.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <exception>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

// Whether `vis` names `level` in a walk-across slot (Warp -1).
bool walks_to(const std::pair<std::array<int, 8>, std::array<int, 8>>& vis, int level) {
    for (std::size_t slot = 0; slot < 8; ++slot)
        if (vis.first[slot] == level && vis.second[slot] == -1) return true;
    return false;
}

}  // namespace

int main() try {
    using namespace d2d::rules::level_ids;
    const std::string text = "Id\n109\n110\n111\n112\n";
    const d2d::txt::Table levels(std::as_bytes(std::span(text.data(), text.size())));
    // Harrogath, the Bloody Foothills, the Frigid Highlands, the Arreat Plateau in a row, each touching the next.
    std::vector<d2d::drlg::Placed> layout;
    for (int level = kHarrogath; level <= kArreatPlateau; ++level) layout.push_back({ level, (level - kHarrogath) * 10, 0, 10, 10 });

    const auto made = d2d::drlg::level_vis(levels, layout, kFrigidHighlands);
    assert(walks_to(made, kArreatPlateau));
    assert(walks_to(made, kBloodyFoothills));                    // the act made: 110 named
    const auto neighbours = d2d::drlg::level_vis(levels, layout, kFrigidHighlands, true);
    assert(walks_to(neighbours, kArreatPlateau));
    assert(!walks_to(neighbours, kBloodyFoothills));             // FUN_00677680's pass: not yet
    assert(walks_to(d2d::drlg::level_vis(levels, layout, kBloodyFoothills), kFrigidHighlands));
    std::puts("OK: the Frigid Highlands' Vis names the Bloody Foothills once the act is made");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
