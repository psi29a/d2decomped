// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A shift-click's pick of a monster beside the player (SearchEnemyXY) over the units in the order
// FUN_00641cb0 walks them: the near rooms' list, each room's units newest first. d2d's
// rules::search_enemy_xy_walk against game.exe's own run (tools/emu/search_walk.py writes the
// cases). With a case file (`search_walk.py gen`) it prints `id pick` for each line, as the
// oracle's `game` does.
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

struct Word { int x, y, bits; };
// A unit as the oracle's case has it: SearchUnit's fields, then its room stamp.
struct Listed { SearchUnit unit; std::uint32_t stamp; };

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

bool has_unit_bit(std::span<const Word> words, int spot_x, int spot_y) {
    for (const auto& word : words)
        if (word.x == spot_x && word.y == spot_y) return (word.bits & 0x180) != 0;
    return false;
}

// d2d's pick over the walk (ClientSession::shift_pick's call), or, `walked` false, the units as they came.
int pick(int dir, int player_x, int player_y, bool targetable_only, std::span<const Listed> units, std::span<const Word> words, bool walked = true) {
    std::vector<SearchUnit> search;
    std::vector<RoomUnit> listed;
    for (const auto& unit : units) {
        search.push_back(unit.unit);
        listed.push_back({ room_id(unit.unit.x, unit.unit.y), true, unit.stamp });
    }
    const auto bits = [&](int spot_x, int spot_y) { return has_unit_bit(words, spot_x, spot_y); };
    if (!walked) return search_enemy_xy(dir, player_x, player_y, targetable_only, search, bits);
    return search_enemy_xy_walk(dir, player_x, player_y, targetable_only, search, listed, near_ids(player_x, player_y), bits);
}

int print_file(const char* path) {
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        int case_id = 0, dir = 0, player_x = 0, player_y = 0, tonly = 0, count = 0;
        if (!(fields >> case_id >> dir >> player_x >> player_y >> tonly >> count)) continue;
        // Bounded so a malformed line can't run a search away: search_walk.py's cases sit in a
        // 200 x 200 level, a 64-step direction, a few units and words.
        dir = std::clamp(dir, 0, 63);
        player_x = std::clamp(player_x, 0, kRoom * kGrid - 1); player_y = std::clamp(player_y, 0, kRoom * kGrid - 1);
        count = std::clamp(count, 0, 64);
        std::vector<Listed> units(static_cast<std::size_t>(count));
        for (auto& unit : units) {
            int flag4 = 0, shift_sel = 0, interact = 0, stamp = 0;
            fields >> unit.unit.x >> unit.unit.y >> unit.unit.size >> unit.unit.type >> unit.unit.mode >> flag4 >> shift_sel >> interact >> stamp;
            unit.unit.flag4 = flag4 != 0; unit.unit.shift_sel = shift_sel != 0; unit.unit.interact = interact != 0;
            unit.stamp = static_cast<std::uint32_t>(stamp);
            unit.unit.x = std::clamp(unit.unit.x, 0, kRoom * kGrid - 1); unit.unit.y = std::clamp(unit.unit.y, 0, kRoom * kGrid - 1);
        }
        fields >> count;
        count = std::clamp(count, 0, 64);
        std::vector<Word> words(static_cast<std::size_t>(count));
        for (auto& word : words) fields >> word.x >> word.y >> word.bits;
        std::printf("%d %d\n", case_id, pick(dir, player_x, player_y, tonly != 0, units, words));
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) return print_file(argv[1]);
    struct UnitCase { int x, y, size, type, mode, flag4, shift_sel, interact; std::uint32_t stamp; };
    struct Case {
        int dir, player_x, player_y, tonly, unit_count;
        std::array<UnitCase, 6> units;
        int word_count;
        std::array<Word, 48> words;
        int pick;
    };
    static const Case cases[]{
#include "search_walk_cases.inc"
    };
    int differ = 0, view_order_differs = 0;
    for (const auto& sample : cases) {
        std::vector<Listed> units;
        for (const auto& unit : std::span(sample.units).first(std::size_t(sample.unit_count)))
            units.push_back({ SearchUnit{ unit.x, unit.y, unit.size, unit.type, unit.mode, unit.flag4 != 0, unit.shift_sel != 0, unit.interact != 0 }, unit.stamp });
        const auto words = std::span(sample.words).first(std::size_t(sample.word_count));
        const int got = pick(sample.dir, sample.player_x, sample.player_y, sample.tonly != 0, units, words);
        if (got != sample.pick) {
            std::printf("differ: dir %d at (%d, %d) tonly %d: game.exe %d, d2d %d\n", sample.dir, sample.player_x, sample.player_y, sample.tonly, sample.pick, got);
            ++differ;
        }
        view_order_differs += pick(sample.dir, sample.player_x, sample.player_y, sample.tonly != 0, units, words, false) != sample.pick;
    }
    std::printf("%zu cases, %d differ\n", sizeof(cases) / sizeof(cases[0]), differ);
    assert(differ == 0);
    // The fixture must exercise the order: the units as the client lists them (view order) pick another
    // unit in some cases, which is what d2d did before it walked the near rooms' lists.
    std::printf("%d cases the view order gets wrong\n", view_order_differs);
    assert(view_order_differs > 0);
    // Two monsters on one spot's 3 x 3: the one newer in its room's list is the pick, whatever their order in the view.
    const std::array<Word, 1> bit{ Word{ 100, 103, 0x100 } };
    const std::array<Listed, 2> pair{ Listed{ SearchUnit{ 100, 103, 3 }, 1 }, Listed{ SearchUnit{ 101, 103, 3 }, 2 } };
    assert(pick(0, 100, 100, false, pair, bit) == 1);
    assert(pick(0, 100, 100, false, pair, bit, false) == 0);
    return differ == 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::printf("error: %s\n", error.what());
    return 1;
}
