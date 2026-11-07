// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Show Items' label layout (FUN_004c0640, 0x4c0640, in FUN_004c0810's
// loop) against stock 1.14d's, run by tools/emu/show_items_layout.py:
// 500 screens of floor items. No game data needed at runtime. With a
// cases file argument (show_items_layout.py's `gen` output) it prints its
// `game` lines instead, to diff.
#include <show_items.hpp>

#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

d2d::rules::LabelRect built(int x, int y, int text_w, int text_h) {
    const int box_w = text_w + 8;
    return { x - (box_w >> 1), y - text_h + 4, x - (box_w >> 1) + box_w, y };
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        std::ifstream input(argv[1]);
        for (std::string line; std::getline(input, line);) {
            std::istringstream fields(line);
            int scene = 0, width = 0, height = 0, min_x = 0, max_x = 0, count = 0;
            fields >> scene >> width >> height >> min_x >> max_x >> count;
            if (count < 0 || count > 1024) continue;        // a scene line is at most a screen of labels
            std::vector<d2d::rules::LabelRect> placed;
            for (int item = 0; item < count; ++item) {
                int x = 0, y = 0, text_w = 0, text_h = 0;
                fields >> x >> y >> text_w >> text_h;
                auto rect = built(x, y, text_w, text_h);
                bool kept = false;
                const int tries = d2d::rules::label_place(rect, placed, x, y, max_x, min_x, height, kept);
                std::printf("%d %d %d %d %d %d %d %d\n", scene, item, tries, int(kept), rect.left, rect.top, rect.right, rect.bottom);
            }
        }
        return 0;
    }
    struct Case { int scene, width, height, min_x, max_x, x, y, text_w, text_h, tries, kept, left, top, right, bottom; };
    const Case cases[] = {
#include "show_items_layout_cases.inc"
    };
    std::vector<d2d::rules::LabelRect> placed;
    int scene = -1;
    for (const auto& row : cases) {
        if (row.scene != scene) { scene = row.scene; placed.clear(); }
        auto rect = built(row.x, row.y, row.text_w, row.text_h);
        bool kept = false;
        const int tries = d2d::rules::label_place(rect, placed, row.x, row.y, row.max_x, row.min_x, row.height, kept);
        assert(tries == row.tries && int(kept) == row.kept && rect.left == row.left && rect.top == row.top && rect.right == row.right && rect.bottom == row.bottom);
    }
    std::printf("OK: %zu labels as game.exe's\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
}
