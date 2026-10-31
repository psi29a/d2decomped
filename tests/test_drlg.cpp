// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: drlg
// Acts 1 and 2's level layout: for many seeds, every level clear of the others
// and touching its link, the town where game.exe allows it, reproducible.
#include <drlg.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <utility>

using namespace d2d::drlg;

int main() {
    const LevelDefs defs = {
        { 1, { 56, 40 } }, { 2, { 80, 80, 0, 0, true } }, { 3, { 80, 80, 0, 0, true } },
        { 4, { 80, 80, 1000, 1000, true } }, { 17, { 40, 48, 0, 0, true } },
        { 39, { 80, 80, 0, 0, true } }, { 26, { 64, 18, 1000, 1000 } },
        { 5, { 80, 80, 0, 0, true } }, { 6, { 80, 80, 0, 0, true } }, { 7, { 80, 80, 0, 0, true } } };
    auto touching = [](const Placed& first, const Placed& second) { return apart(first, second, 0) && !apart(first, second, 1); };
    int files[4] = {};
    for (std::uint32_t seed = 1; seed < 400; ++seed) {
        const auto chain = place_chain(kAct1Outdoors, defs, d2d::rules::Rng{ seed }, true);
        assert(chain.size() == kAct1Outdoors.size());
        for (std::size_t i = 0; i < chain.size(); ++i) {
            const int link = kAct1Outdoors[i].link;
            for (std::size_t j = 0; j < i; ++j)
                if (int(j) != link) assert(apart(chain[i], chain[j]));
            if (link >= 0) assert(touching(chain[i], chain[std::size_t(link)]));
        }
        const auto& blood_moor = chain[2];
        const auto& town = chain[3];
        assert(blood_moor.level == 2 && (blood_moor.width == 56 || blood_moor.width == 96) && blood_moor.width + blood_moor.height == 152);
        assert(kTownAllowed[std::size_t(town.dir + 4 * town.flip + 8 * blood_moor.dir + 32 * blood_moor.flip)]);
        const int town_file_index = town_file(chain);
        assert(town_file_index >= 0 && town_file_index < 4);
        ++files[town_file_index];
        // Same seed, same act.
        const auto again = place_chain(kAct1Outdoors, defs, d2d::rules::Rng{ seed }, true);
        for (std::size_t i = 0; i < chain.size(); ++i) assert(again[i].x == chain[i].x && again[i].y == chain[i].y);
    }
    std::printf("town files N/E/S/W: %d %d %d %d\n", files[0], files[1], files[2], files[3]);
    assert(files[0] + files[1] + files[2] + files[3] == 399);
    // Act 2 (FUN_00677790): Lut Gholein anchors a chain of five, each
    // clear of the rest and touching its link; the town's file is the Rocky
    // Waste's side; the two tombs differ.
    LevelDefs act2 = { { 40, { 56, 56, 1000, 1000 } }, { 45, { 32, 32, 0, 0, true } }, { 46, { 80, 80, 2500, 1000, true } } };
    for (int id : { 41, 42, 43, 44 }) act2[id] = { 80, 80, 0, 0, true };
    int lut_files[3] = {};
    for (std::uint32_t seed = 1; seed < 400; ++seed) {
        auto act_seed = d2d::rules::Rng{ seed };
        const auto layout = act2_layout(act2, act_seed);
        assert(layout.size() == kAct2Outdoors.size() + kAct2Canyon.size());
        for (std::size_t i = 0; i < kAct2Outdoors.size(); ++i) {
            const int link = kAct2Outdoors[i].link;
            for (std::size_t j = 0; j < i; ++j)
                if (int(j) != link) assert(apart(layout[i], layout[j]));
            if (link >= 0) assert(touching(layout[i], layout[std::size_t(link)]));
        }
        const int file = lut_gholein_file(layout);
        assert(file == 1 || file == 2);
        ++lut_files[file];
        const auto tombs = roll_act2_tombs(act_seed);
        assert(tombs.boss != tombs.staff && tombs.boss >= 66 && tombs.staff <= 72);
    }
    assert(lut_files[1] && lut_files[2]);
    // Act 3 (FUN_006789b0; diffed 2000/2000 by diff_drlg.py 1-2000 75 layout):
    // the jungle's three pieces clear of each other, 2x6 cells of jungle
    // Defs each, the Spider Forest lowest; Kurast stacked up from the top.
    LevelDefs act3 = { { 75, { 64, 48, 1000, 1000 } }, { 82, { 48, 16, 0, 0, true } }, { 83, { 64, 64, 0, 0, true } } };
    for (int id : { 76, 77, 78 }) act3[id] = { 64, 192, 0, 0, true };
    for (int id : { 79, 80, 81 }) act3[id] = { 80, 64, 0, 0, true };
    for (std::uint32_t seed = 1; seed < 400; ++seed) {
        const auto layout = act3_layout(act3, d2d::rules::Rng{ seed });
        assert(!layout.failed && layout.levels.size() == 9);
        for (std::size_t i = 1; i <= 3; ++i) {
            for (std::size_t j = 1; j < i; ++j) assert(apart(layout.levels[i], layout.levels[j]));
            assert(layout.levels[i].y <= layout.levels[1].y && layout.jungle[i - 1].presets.size() == 12);
            for (const int def_id : layout.jungle[i - 1].presets) assert(def_id == 0 || (def_id >= 530 && def_id <= 584));
        }
        for (std::size_t i = 4; i < 9; ++i) assert(layout.levels[i].y + layout.levels[i].height == layout.levels[i - 1].y);
    }
    // No jungle row (or one too small): a failed layout, not a hang.
    auto no_jungle = std::move(act3);
    no_jungle.erase(76);
    assert(act3_layout(no_jungle, d2d::rules::Rng{ 1 }).failed);
    // Act 4 (FUN_00678a20): Pandemonium Fortress anchors Outer Steppes,
    // Plains of Despair, City of the Damned; Chaos Sanctuary anchors its
    // chain. Outer Steppes rolls flip 1 (y 968, 0x800000) or flip 0 (y 992, 0x400000).
    LevelDefs act4 = {
        { 103, { 40, 40, 1000, 1000 } }, { 104, { 80, 80, 0, 0, true } },
        { 105, { 80, 80, 0, 0, true } }, { 106, { 80, 80, 0, 0, true } },
        { 108, { 120, 120, 1500, 1000 } } };
    int steppes_flips[2] = {};
    for (std::uint32_t seed = 1; seed < 400; ++seed) {
        const auto layout = act4_layout(act4, d2d::rules::Rng{ seed });
        assert(layout.size() == 5);
        assert(layout[0].level == 103 && layout[0].x == 1000 && layout[0].y == 1000);
        assert(layout[1].level == 104 && layout[1].x == 1040);
        assert((layout[1].flip == 1 && layout[1].y == 968 && layout[1].flags == 0x800000)
            || (layout[1].flip == 0 && layout[1].y == 992 && layout[1].flags == 0x400000));
        ++steppes_flips[layout[1].flip];
        for (std::size_t i = 0; i < 4; ++i) {
            const int link = kAct4Outdoors[i].link;
            for (std::size_t j = 0; j < i; ++j)
                if (int(j) != link) assert(apart(layout[i], layout[j]));
            if (link >= 0) assert(touching(layout[i], layout[std::size_t(link)]));
        }
        assert(layout[4].level == 108 && layout[4].x == 1500 && layout[4].y == 1000);
        // Reproducibility
        const auto again = act4_layout(act4, d2d::rules::Rng{ seed });
        for (std::size_t i = 0; i < 5; ++i) {
            assert(again[i].x == layout[i].x && again[i].y == layout[i].y);
            assert(again[i].flags == layout[i].flags && again[i].dir == layout[i].dir);
        }
    }
    assert(steppes_flips[0] > 0 && steppes_flips[1] > 0);
    std::puts("test_drlg: ok");
}
