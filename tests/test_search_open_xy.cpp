// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Where a SearchOpenXY skill goes (Fire Wall, Meteor, Blizzard, Eruption,
// Volcano): game::search_open_xy against game.exe's FUN_004677b0 run by its
// own code (tools/emu/click_free_spot.py writes the cases). With a case file
// (`click_free_spot.py gen`) it prints `id kept x y` for each line, as the
// oracle's `game` does.
#include <gamedata.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <exception>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using namespace d2d::game;

namespace {

constexpr int kWorld = 200;                      // sk_tele_spot.py's world: 5 x 5 rooms of 40 subtiles

struct Rect { int x0, y0, x1, y1, bits; };

// FUN_004677b0's answer: kept (the target back unchanged), and the click's x, y as it leaves them.
std::tuple<int, int, int> select(int kind, int target_x, int target_y, int click_x, int click_y, std::span<const Rect> rects) {
    const auto flags = [&](int at_x, int at_y) {
        if (at_x < 0 || at_y < 0 || at_x >= kWorld || at_y >= kWorld) return 0x27;
        int bits = 0;
        for (const auto& rect : rects)
            if (at_x >= rect.x0 && at_x <= rect.x1 && at_y >= rect.y0 && at_y <= rect.y1) bits = rect.bits;
        return bits;
    };
    const auto spot = kind != 0 ? search_open_xy(target_x, target_y, flags) : search_open_xy(click_x, click_y, flags);
    if (spot) return { 0, spot->first, spot->second };
    return { kind != 0 ? 1 : 0, click_x, click_y };
}

int print_file(const char* path) {
    std::ifstream file(path);
    if (!file) {
        std::printf("error: cannot read %s\n", path);
        return 1;
    }
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        int case_id = 0, kind = 0, target_x = 0, target_y = 0, click_x = 0, click_y = 0, count = 0;
        if (!(fields >> case_id >> kind >> target_x >> target_y >> click_x >> click_y >> count)) continue;
        std::vector<Rect> rects(std::size_t(count > 0 ? count : 0));
        for (auto& rect : rects) fields >> rect.x0 >> rect.y0 >> rect.x1 >> rect.y1 >> rect.bits;
        const auto [kept, out_x, out_y] = select(kind, target_x, target_y, click_x, click_y, rects);
        std::printf("%d %d %d %d\n", case_id, kept, out_x, out_y);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) return print_file(argv[1]);
    struct Case {
        int kind, target_x, target_y, click_x, click_y, rect_count;
        std::array<Rect, 5> rects;
        int kept, out_x, out_y;
    };
    static const Case cases[]{
#include "search_open_xy_cases.inc"
    };
    int differ = 0;
    for (const auto& sample : cases) {
        const auto got = select(sample.kind, sample.target_x, sample.target_y, sample.click_x, sample.click_y, std::span(sample.rects).first(std::size_t(sample.rect_count)));
        if (got != std::tuple(sample.kept, sample.out_x, sample.out_y)) {
            std::printf("differ: kind %d target (%d, %d) click (%d, %d): game.exe %d (%d, %d), d2d %d (%d, %d)\n", sample.kind, sample.target_x, sample.target_y, sample.click_x,
                        sample.click_y, sample.kept, sample.out_x, sample.out_y, std::get<0>(got), std::get<1>(got), std::get<2>(got));
            ++differ;
        }
    }
    std::printf("%zu cases, %d differ\n", sizeof(cases) / sizeof(cases[0]), differ);
    assert(differ == 0);
    // Open ground: nothing moves. A wall under the click: the nearest subtile off it, ring 2's left column middle.
    assert(select(0, 0, 0, 100, 100, {}) == std::tuple(0, 100, 100));
    const std::array<Rect, 1> block{ Rect{ 99, 99, 101, 101, 1 } };
    assert(select(0, 0, 0, 100, 100, block) == std::tuple(0, 98, 100));
    // Walls 6 out all round: nothing within the search, the monster stays the target.
    const std::array<Rect, 1> wide{ Rect{ 94, 94, 106, 106, 1 } };
    assert(select(1, 100, 100, 50, 60, wide) == std::tuple(1, 50, 60));
    return differ == 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::printf("error: %s\n", error.what());
    return 1;
}
