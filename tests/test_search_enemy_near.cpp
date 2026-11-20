// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A click's pick of the unit nearest it (Skills.txt SearchEnemyNear),
// rules::search_enemy_near against game.exe's FUN_00467490 run by its own
// code (tools/emu/search_enemy_near.py writes the cases). With a case file
// (`search_enemy_near.py gen`) it prints `id pick x y` for each line, as the
// oracle's `game` does.
#include <search_enemy.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <exception>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <vector>

using namespace d2d::rules;

namespace {

struct Unit { int type, x, y, mode, dead, hit, metal; };

struct Pick { int index, x, y; };

// Kind 0 a plain skill, 1 TargetCorpse, 2 Iron Golem; unit 0 is the local player.
Pick pick(int kind, int town, int target, int click_x, int click_y, std::span<const Unit> rows) {
    std::vector<NearUnit> units;
    units.reserve(rows.size());
    for (const auto& row : rows) units.push_back({ row.type, row.x, row.y, row.mode, row.dead != 0, row.hit != 0, row.metal != 0 });
    int spot_x = click_x, spot_y = click_y;
    const int index = search_enemy_near(kind == 1, kind == 2, town != 0, 0, target, spot_x, spot_y, units);
    return { index, spot_x, spot_y };
}

int print_file(const char* path) {
    std::ifstream file(path);
    if (!file) {
        std::fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        int case_id = 0, kind = 0, town = 0, target = -1, click_x = 0, click_y = 0, count = 0;
        if (!(fields >> case_id >> kind >> town >> target >> click_x >> click_y >> count)) continue;
        // Bounded so a malformed line can't run a search away: search_enemy_near.py's cases sit
        // round (100, 100), 8 units at most.
        count = std::clamp(count, 0, 64);
        target = std::clamp(target, -1, count - 1);
        click_x = std::clamp(click_x, -0x400, 0x400); click_y = std::clamp(click_y, -0x400, 0x400);
        std::vector<Unit> units(static_cast<std::size_t>(count));
        for (auto& unit : units) fields >> unit.type >> unit.x >> unit.y >> unit.mode >> unit.dead >> unit.hit >> unit.metal;
        const auto got = pick(kind, town, target, click_x, click_y, units);
        std::printf("%d %d %d %d\n", case_id, got.index, got.x, got.y);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) return print_file(argv[1]);
    struct Case {
        int kind, town, target, click_x, click_y, unit_count;
        std::array<Unit, 8> units;
        int pick, x, y;
    };
    static const Case cases[]{
#include "search_enemy_near_cases.inc"
    };
    int differ = 0;
    for (const auto& sample : cases) {
        const auto got = pick(sample.kind, sample.town, sample.target, sample.click_x, sample.click_y, std::span(sample.units).first(std::size_t(sample.unit_count)));
        if (got.index != sample.pick || got.x != sample.x || got.y != sample.y) {
            std::printf("differ: kind %d town %d target %d at (%d, %d): game.exe %d (%d, %d), d2d %d (%d, %d)\n", sample.kind, sample.town, sample.target,
                        sample.click_x, sample.click_y, sample.pick, sample.x, sample.y, got.index, got.x, got.y);
            ++differ;
        }
    }
    std::printf("%zu cases, %d differ\n", sizeof(cases) / sizeof(cases[0]), differ);
    assert(differ == 0);
    // The nearest under 10 wins, the first of equals: two monsters 3 and 2 away, then one 2 away again.
    const std::array<Unit, 4> row{ { { 0, 50, 50, 1, 0, 1, 0 }, { 1, 63, 60, 1, 0, 1, 0 }, { 1, 60, 62, 1, 0, 1, 0 }, { 1, 62, 60, 1, 0, 1, 0 } } };
    assert(pick(0, 0, -1, 60, 60, row).index == 2);
    // A corpse skill takes only the dead (mode 0xc); a town takes nothing but Iron Golem's metal item.
    const std::array<Unit, 3> dead{ { { 0, 50, 50, 1, 0, 1, 0 }, { 1, 61, 60, 1, 0, 1, 0 }, { 1, 63, 60, 0xc, 0, 1, 0 } } };
    assert(pick(1, 0, -1, 60, 60, dead).index == 2);
    assert(pick(0, 1, -1, 60, 60, dead).index == -1);
    const std::array<Unit, 2> item{ { { 0, 50, 50, 1, 0, 1, 0 }, { 4, 66, 60, 0, 0, 1, 1 } } };
    assert(pick(2, 1, -1, 60, 60, item).index == 1);
    std::puts("search_enemy_near ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
