// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A shift-click's pick of a monster beside the player (Skills.txt
// SearchEnemyXY), rules::search_enemy_xy against game.exe's FUN_00467660 run
// by its own code (tools/emu/search_enemy_xy.py writes the cases). With a
// case file (`search_enemy_xy.py gen`) it prints `id pick` for each line, as
// the oracle's `game` does.
#include <search_enemy.hpp>

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

struct Word { int x, y, bits; };

int pick(int dir, int player_x, int player_y, bool targetable_only, std::span<const SearchUnit> units, std::span<const Word> words) {
    return search_enemy_xy(dir, player_x, player_y, targetable_only, units, [&](int spot_x, int spot_y) {
        for (const auto& word : words)
            if (word.x == spot_x && word.y == spot_y) return (word.bits & 0x180) != 0;
        return false;
    });
}

int print_file(const char* path) {
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        int case_id = 0, dir = 0, player_x = 0, player_y = 0, tonly = 0, count = 0;
        if (!(fields >> case_id >> dir >> player_x >> player_y >> tonly >> count)) continue;
        std::vector<SearchUnit> units(std::size_t(count > 0 ? count : 0));
        for (auto& unit : units) {
            int flag4 = 0, shift_sel = 0, interact = 0;
            fields >> unit.x >> unit.y >> unit.size >> unit.type >> unit.mode >> flag4 >> shift_sel >> interact;
            unit.flag4 = flag4 != 0; unit.shift_sel = shift_sel != 0; unit.interact = interact != 0;
        }
        fields >> count;
        std::vector<Word> words(std::size_t(count > 0 ? count : 0));
        for (auto& word : words) fields >> word.x >> word.y >> word.bits;
        std::printf("%d %d\n", case_id, pick(dir, player_x, player_y, tonly != 0, units, words));
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) return print_file(argv[1]);
    struct Case {
        int dir, player_x, player_y, tonly, unit_count;
        std::array<SearchUnit, 6> units;
        int word_count;
        std::array<Word, 12> words;
        int pick;
    };
    static const Case cases[]{
#include "search_enemy_xy_cases.inc"
    };
    int differ = 0;
    for (const auto& sample : cases) {
        const int got = pick(sample.dir, sample.player_x, sample.player_y, sample.tonly != 0, std::span(sample.units).first(std::size_t(sample.unit_count)),
                             std::span(sample.words).first(std::size_t(sample.word_count)));
        if (got != sample.pick) {
            std::printf("differ: dir %d at (%d, %d) tonly %d: game.exe %d, d2d %d\n", sample.dir, sample.player_x, sample.player_y, sample.tonly, sample.pick, got);
            ++differ;
        }
    }
    std::printf("%zu cases, %d differ\n", sizeof(cases) / sizeof(cases[0]), differ);
    assert(differ == 0);
    // The facing's eighth first, then one either way, then two: facing 0 (+y) tries (0, +3) first.
    const std::array<SearchUnit, 2> beside{ SearchUnit{ 100, 103, 1 }, SearchUnit{ 98, 102, 1 } };
    assert(pick(0, 100, 100, false, beside, std::array<Word, 2>{ Word{ 100, 103, 0x100 }, Word{ 98, 102, 0x100 } }) == 0);
    // No unit bit on the spot (0x180), no pick there, even with a monster on it.
    assert(pick(0, 100, 100, false, beside, std::array<Word, 1>{ Word{ 98, 102, 0x1000 } }) == -1);
    return differ == 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::printf("error: %s\n", error.what());
    return 1;
}
