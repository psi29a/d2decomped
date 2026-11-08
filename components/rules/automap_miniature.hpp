// SPDX-License-Identifier: GPL-3.0-or-later
// The town miniatures: Lut Gholein, the Pandemonium Fortress and
// Harrogath show on the automap as a picture, a grid of cels from their
// own cel file, not as their tiles' cels (FUN_004591a0, which the DRLG
// calls as the town level comes up: FUN_00668100 -> drlg +0x488, set by
// FUN_0044e100). docs/research/re/automap.md "Town miniatures".
#pragma once

#include <algorithm>
#include <array>
#include <vector>

namespace d2d::rules {

// The layer's cel file for a town's miniature (layer +4; FUN_0045a2b0
// loads them): 1 UI\AutoMap\Act2Map, 2 Act4Map, 3 ExTnMap ("...S" for the
// small map); 0 for any other level (MaxiMap, the tiles' cels).
constexpr int miniature_cel_file(int level) {
    return level == 0x28 ? 1 : level == 0x67 ? 2 : level == 0x6d ? 3 : 0;
}

struct MiniatureCell { int cel, x, y; };   // list 3: cel, and x, y in automap px (client px / 10)

// The cells FUN_004591a0 adds for `level` (0x28, 0x67 or 0x6d; none for
// another), its preset's file `preset_file` (Lut Gholein's: 1 LutW, cels
// 0..19; else LutN, 20..39) and its middle `centre_x`, `centre_y` in act
// tiles (the level's x + w / 2, y + h / 2: FUN_00668100). The middle goes
// to client px (FUN_00643310: (x - y) * 80, (x + y) * 40) and over 10;
// the grid's top left sits half the grid up and left of it, less an offset
// per town; each cel one step on. Lut Gholein skips the cels listed at
// 0x6d6608 (its blank corners), keeping their place.
inline std::vector<MiniatureCell> automap_miniature(int level, int preset_file, int centre_x, int centre_y) {
    struct Town { int columns, rows, offset_x, offset_y, step_x, step_y; };
    Town town{};
    int first_cel = 0;
    bool skips = false;
    if (level == 0x28) {
        town = { 5, 4, 0x35, -0x51, 0xa0, 100 };
        first_cel = preset_file == 1 ? 0 : 0x14;
        skips = true;
    } else if (level == 0x67) {
        town = { 2, 2, -3, -0x32, 0x88, 0x5a };
    } else if (level == 0x6d) {
        town = { 2, 2, -0x14, -0x9b, 0xb4, 0xaa };
    } else {
        return {};
    }
    static constexpr std::array kSkipped{ 0, 10, 15, 16, 19, 20, 25, 30, 35, 36, 39 };   // 0x6d6608, to -1
    const int client_x = (centre_x - centre_y) * 0x50, client_y = (centre_y + centre_x) * 0x50 >> 1;
    const int middle_x = client_x / 10, middle_y = client_y / 10;
    std::vector<MiniatureCell> cells;
    int cel = first_cel;
    int y = middle_y - town.step_y * town.rows / 2 - town.offset_y;
    for (int row = 0; row < town.rows; ++row, y += town.step_y) {
        int x = middle_x - (town.step_x * town.columns / 2 + town.offset_x);
        for (int column = 0; column < town.columns; ++column, ++cel, x += town.step_x)
            if (!skips || std::ranges::find(kSkipped, cel) == kSkipped.end()) cells.push_back({ cel, x, y });
    }
    return cells;
}

}  // namespace d2d::rules
