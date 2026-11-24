// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A click's pick of the unit nearest it (Skills.txt SearchEnemyNear) over the units in the order the
// unit finder FUN_0065ac70 walks them: the near rooms' list, each room's units newest first. d2d's
// rules::search_enemy_near_walk against game.exe's own run of FUN_00467490 (tools/emu/search_walk_near.py
// writes the cases). With a case file (`search_walk_near.py gen`) it prints `id pick x y` for each line,
// as the oracle's `game` does.
#include <search_walk.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <vector>

using namespace d2d::rules;

namespace {

struct Unit { int type, x, y, mode, dead, hit, metal; std::uint32_t stamp; };
struct Pick { int index, x, y; };

constexpr int kRoom = 40, kGrid = 5;   // the oracle's level: 5 x 5 rooms of 40 subtiles

int room_id(int x, int y) { return (x / kRoom) * kGrid + y / kRoom; }

// The player's room's near list in the oracle's order: the rooms touching it, x first.
std::vector<int> near_ids(int player_x, int player_y) {
    std::vector<int> rooms;
    for (int across = player_x / kRoom - 1; across <= player_x / kRoom + 1; ++across)
        for (int down = player_y / kRoom - 1; down <= player_y / kRoom + 1; ++down)
            if (across >= 0 && across < kGrid && down >= 0 && down < kGrid) rooms.push_back(across * kGrid + down);
    return rooms;
}

// Kind 0 a plain skill, 1 TargetCorpse, 2 Iron Golem; unit 0 is the local player. `walked` false: the units in
// the order they came (what d2d did before it walked the rooms).
Pick pick(int kind, int town, int target, int click_x, int click_y, std::span<const Unit> rows, bool walked = true) {
    std::vector<NearUnit> units;
    std::vector<RoomUnit> listed;
    for (const auto& row : rows) {
        units.push_back({ row.type, row.x, row.y, row.mode, row.dead != 0, row.hit != 0, row.metal != 0 });
        listed.push_back({ room_id(row.x, row.y), true, row.stamp });
    }
    int spot_x = click_x, spot_y = click_y;
    const int index = walked ? search_enemy_near_walk(kind == 1, kind == 2, town != 0, 0, target, spot_x, spot_y, units, listed, near_ids(rows[0].x, rows[0].y))
                             : search_enemy_near(kind == 1, kind == 2, town != 0, 0, target, spot_x, spot_y, units);
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
        // Bounded so a malformed line can't run a search away: search_walk_near.py's cases sit in a
        // 200 x 200 level, 8 units at most.
        count = std::clamp(count, 1, 64);
        target = std::clamp(target, -1, count - 1);
        click_x = std::clamp(click_x, 0, kRoom * kGrid - 1); click_y = std::clamp(click_y, 0, kRoom * kGrid - 1);
        std::vector<Unit> units(static_cast<std::size_t>(count));
        for (auto& unit : units) {
            int stamp = 0;
            fields >> unit.type >> unit.x >> unit.y >> unit.mode >> unit.dead >> unit.hit >> unit.metal >> stamp;
            unit.stamp = static_cast<std::uint32_t>(stamp);
            unit.x = std::clamp(unit.x, 0, kRoom * kGrid - 1); unit.y = std::clamp(unit.y, 0, kRoom * kGrid - 1);
        }
        const auto got = pick(kind, town, target, click_x, click_y, units);
        std::printf("%d %d %d %d\n", case_id, got.index, got.x, got.y);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) return print_file(argv[1]);
    struct UnitCase { int type, x, y, mode, dead, hit, metal, stamp; };
    struct Case {
        int kind, town, target, click_x, click_y, unit_count;
        std::array<UnitCase, 8> units;
        int pick, x, y;
    };
    static const Case cases[]{
#include "search_walk_near_cases.inc"
    };
    int differ = 0, view_order_differs = 0;
    for (const auto& sample : cases) {
        std::vector<Unit> units;
        for (const auto& unit : std::span(sample.units).first(std::size_t(sample.unit_count)))
            units.push_back({ unit.type, unit.x, unit.y, unit.mode, unit.dead, unit.hit, unit.metal, static_cast<std::uint32_t>(unit.stamp) });
        const auto got = pick(sample.kind, sample.town, sample.target, sample.click_x, sample.click_y, units);
        if (got.index != sample.pick || got.x != sample.x || got.y != sample.y) {
            std::printf("differ: kind %d town %d target %d at (%d, %d): game.exe %d (%d, %d), d2d %d (%d, %d)\n", sample.kind, sample.town, sample.target,
                        sample.click_x, sample.click_y, sample.pick, sample.x, sample.y, got.index, got.x, got.y);
            ++differ;
        }
        const auto plain = pick(sample.kind, sample.town, sample.target, sample.click_x, sample.click_y, units, false);
        view_order_differs += plain.index != sample.pick || plain.x != sample.x || plain.y != sample.y;
    }
    std::printf("%zu cases, %d differ\n", sizeof(cases) / sizeof(cases[0]), differ);
    assert(differ == 0);
    // The fixture must exercise the order: the units as the client lists them (view order) pick another
    // unit in some cases, which is what d2d did before it walked the near rooms' lists.
    std::printf("%d cases the view order gets wrong\n", view_order_differs);
    assert(view_order_differs > 0);
    // Two monsters equally near the click: the one newer in its room's list is the pick, whatever their order in the view.
    const std::array<Unit, 3> tie{ { { 0, 50, 50, 1, 0, 1, 0, 1 }, { 1, 61, 60, 1, 0, 1, 0, 1 }, { 1, 59, 60, 1, 0, 1, 0, 2 } } };
    assert(pick(0, 0, -1, 60, 60, tie).index == 2);
    assert(pick(0, 0, -1, 60, 60, tie, false).index == 1);
    // Equally near units in two rooms: the near list's order, not the stamps' (the room to the left, x 39, is
    // first in the oracle's list, the player's own room second).
    const std::array<Unit, 3> rooms{ { { 0, 41, 60, 1, 0, 1, 0, 1 }, { 1, 43, 60, 1, 0, 1, 0, 1 }, { 1, 39, 60, 1, 0, 1, 0, 9 } } };
    assert(pick(0, 0, -1, 41, 60, rooms).index == 2);
    std::puts("search_walk_near ok");
    return differ == 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::printf("error: %s\n", error.what());
    return 1;
}
