// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Show Items' labels (FUN_004c0810, 0x4c0810: which items, in what order,
// where and lit) against stock 1.14d's, run by tools/emu/show_items_walk.py:
// 300 screens of near rooms and their units. No game data needed at
// runtime. With a cases file argument (show_items_walk.py's `gen` output)
// it prints its `game` lines instead, to diff.
#include <show_items.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Drawn { int unit, left, top, right, bottom, lit; };

// One case line: the walk (the rooms in order, each from its head; the
// drawn items, type 4), then rules::show_items on it.
std::vector<Drawn> screen(const std::string& line, int& scene) {
    std::istringstream fields(line);
    int width = 0, height = 0, side = 0, mouse_x = 0, mouse_y = 0, cursor = 0, rooms = 0;
    fields >> scene >> width >> height >> side >> mouse_x >> mouse_y >> cursor >> rooms;
    rooms = std::clamp(rooms, 0, 64);                // a malformed line can't run away: the cases hold 9 rooms of 20 at most
    std::vector<d2d::rules::LabelItem> walk;
    std::vector<int> unit_of;
    std::ptrdiff_t cursor_item = -1;
    for (int room = 0, number = 0; room < rooms; ++room) {
        int count = 0;
        fields >> count;
        count = std::clamp(count, 0, 256);
        for (int unit = 0; unit < count; ++unit, ++number) {
            int kind = 0, drawn = 0, x = 0, y = 0, text_w = 0, text_h = 0;
            fields >> kind >> drawn >> x >> y >> text_w >> text_h;
            if (kind != 4 || !drawn) continue;
            if (number == cursor) cursor_item = std::ptrdiff_t(walk.size());
            walk.push_back(d2d::rules::label_item(x, y, text_w, text_h));   // x y: where it stands (the view at 0)
            unit_of.push_back(number);
        }
    }
    std::vector<Drawn> out;
    for (const auto& label : d2d::rules::show_items(walk, width, height, side, mouse_x, mouse_y, cursor_item))
        out.push_back({ unit_of[label.item], label.rect.left, label.rect.top, label.rect.right, label.rect.bottom, label.lit ? 1 : 0 });
    return out;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) {
        std::ifstream input(argv[1]);
        for (std::string line; std::getline(input, line);) {
            int scene = 0;
            const auto drawn = screen(line, scene);
            for (const auto& label : drawn)
                std::printf("%d %d %d %d %d %d %d\n", scene, label.unit, label.left, label.top, label.right, label.bottom, label.lit);
            std::printf("%d end %zu\n", scene, drawn.size());
        }
        return 0;
    }
    struct Case { const char* line; const char* labels; };
    const Case cases[] = {
#include "show_items_walk_cases.inc"
    };
    std::size_t labels = 0, capped = 0;
    for (const auto& row : cases) {
        int scene = 0;
        const auto drawn = screen(row.line, scene);
        std::string got;
        for (const auto& label : drawn)
            got += std::to_string(label.unit) + ' ' + std::to_string(label.left) + ' ' + std::to_string(label.top) + ' ' + std::to_string(label.right) + ' ' +
                   std::to_string(label.bottom) + ' ' + std::to_string(label.lit) + ';';
        if (got != row.labels) std::fprintf(stderr, "FAIL: scene %d\n  d2d      %s\n  game.exe %s\n", scene, got.c_str(), row.labels);
        assert(got == row.labels);
        labels += drawn.size();
        capped += drawn.size() == 0x20 ? 1 : 0;
    }
    std::printf("OK: %zu screens, %zu labels (%zu screens at 0x20) as game.exe's\n", sizeof(cases) / sizeof(cases[0]), labels, capped);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_show_items_walk: %s\n", error.what());
    return 1;
}
