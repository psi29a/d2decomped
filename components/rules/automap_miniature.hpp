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
// tiles (the level's x + w / 2, y + h / 2: FUN_00668100).
std::vector<MiniatureCell> automap_miniature(int level, int preset_file, int centre_x, int centre_y);

}  // namespace d2d::rules
