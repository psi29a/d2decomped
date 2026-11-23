// SPDX-License-Identifier: GPL-3.0-or-later
// Outdoor levels (DrlgType 3): act 1's wilderness generator — the cell
// grid of presets, borders, roads and fills (FUN_00675360 -> FUN_006807f0
// -> FUN_006750f0) — and the tiles of its rooms (OutRoom.cpp,
// FUN_0067d2d0). docs/research/re/drlg.md "Outdoor levels".
#pragma once

#include "drlg.hpp"
#include "tile_pick.hpp"
#include "units.hpp"

#include <ds1.hpp>
#include <level_ids.hpp>
#include <rules.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::drlg {

// LvlPrest.txt, what the generator needs: size in tiles, file count, the
// loaded File1..6 (null when absent), and the columns that roll units.
struct Preset {
    int width = 0, height = 0, files = 0, scan = 0, pops = 0, level_id = 0, kill_edge = 1;   // level_id: LvlPrest LevelId
    std::uint32_t dt1_mask = 0;                         // which LvlTypes files its rooms load
    std::array<const d2d::ds1::Map*, 6> maps{};
    int populate = 1;                                   // LvlPrest Populate (0: its rooms flagged 0x800000, FUN_00666680)
    int logicals = 0;                                   // LvlPrest Logicals (1: areas by walls, FUN_0066d110)
    int animate = 0;                                    // LvlPrest Animate (1: its animated tiles keep every frame, FUN_0066d440)
    int fill_blanks = 1;                                // LvlPrest FillBlanks (preset +0x1c: blank floors filled, FUN_00666ac0)
};
// LvlSub.txt, one row: the stamp sheet and its odds per theme.
struct Sub {
    int type = 0, check_all = 0, bord_type = 0, grid_size = 1;   // grid_size: cells per DS1 tile (act 5's barricade rules: 2)
    std::uint32_t dt1_mask = 0;
    std::array<int, 5> prob{}, trials{}, max{};
    const d2d::ds1::Map* map = nullptr;
};
struct OutdoorData {
    std::unordered_map<int, Preset> presets;           // by Def
    std::vector<Sub> subs;                              // file order
    const RoomDt1s* dt1s = nullptr;                     // set: stamps pick their shadow tiles (FUN_0066e060)
    UnitIds ids;                                        // how DS1 units' ids map
};
// A walkable neighbour: its placed rect and its slot in this level's Vis.
struct Neighbour { Placed rect; int slot = 0; bool preset = false; };   // preset: DrlgType 2
struct OutdoorLevel {
    Placed rect;                                        // level, tiles, layout flags
    int sub_type = -1, sub_theme = -1, sub_waypoint = -1, sub_shrine = -1;
    int level_type = 2;                                 // Levels.txt LevelType
    std::vector<Neighbour> neighbours;                  // Vis slot order
    Placed town;                                        // the Den goes far from it
    std::vector<int> jungle;                            // act 3's jungle (76-78): its 32-tile cells' Defs (act3_layout)
    int jungle_specials = 0;                            // and how many are clearings (level +0x1b8)
    int act_roll = 0;                                   // act 3's roll (act +0x474): which end Kurast's edge gaps take
};
// A plain room's DS1 words ((8+1)x(8+1), row-major) as game.exe keeps
// them at room data +0x00 / +0x14 / +0x28 (orientation, wall, floor),
// plus stamped shadows; its seed low word and DT1 mask (room +0x50).
struct PlainRoom {
    int x, y;                                           // level-relative tiles
    std::uint32_t flags;                                // +0x18 value
    std::uint32_t seed_low;                             // room +4
    std::uint32_t theme_mask;                           // FUN_006706a0
    std::uint32_t dt1_mask = 0x44103;                   // LevelType 2's files, | chosen LvlSub rows'
    std::array<std::uint32_t, 81> orient{}, wall{}, floor{}, shadow{};
    d2d::rules::Rng seed;                               // the room seed after its init; the tile picks go on from here
    std::vector<RoomTile> tiles;                        // shadows the stamps picked (with OutdoorData::dt1s)
    std::vector<Unit> units;                            // the stamps' objects, room-relative subtiles, newest first
};
struct Outdoor {
    int cells_wide = 0, cells_high = 0;                                 // cells (8x8 tiles)
    std::uint32_t flags = 0;                            // outdoor flags after generation (layout | 0x20, 0x40)
    std::vector<std::uint32_t> g04, g18, g2c;           // preset def, values, flags
    std::vector<std::vector<std::pair<int, int>>> roads; // polylines, act tiles
    d2d::ds1::Map tiles;                                // the level, level-relative
    // Every room the finish allocated (8x8 tiles, presets split the same
    // way), level-relative tiles, with its seed: monsters populate these.
    struct RoomSeed {
        int x = 0, y = 0;
        std::uint32_t seed = 0;
        int width = 8, height = 8, kind = 1;                     // kind 1 plain, 2 preset
        int def = 0, file = 0, preset_x = 0, preset_y = 0;          // a preset room's LvlPrest def, file and the preset's origin
        bool rolled = false;                                        // Scan or Pops: its units rolled at generation, these stayed
        std::vector<Unit> units;
        std::uint32_t seed_high = 666;                              // a maze / preset level room's seed was stepped: its high word (the bring-up roll)
    };
    std::vector<RoomSeed> rooms;
    std::vector<PlainRoom> plain;                       // plain rooms' words, cell order
    std::vector<std::string> notes;                     // what isn't wired up yet
};

// A level's seed: {act seed's low word (after its first step) + id, 666}.
d2d::rules::Rng level_seed(std::uint32_t map_seed, int level);

namespace outdoor_detail {

// Border preset by (1-based row, style column): row 1..4 edges (kind
// left/up/right/down), 5..12 corners; column 0 cliffs, 1 fences (0x6f0620).
inline constexpr std::array<std::array<int, 2>, 13> kBorder = { {
    { 0, 0 }, { 0, 4 }, { 16, 5 }, { 17, 6 }, { 0, 7 }, { 18, 8 }, { 19, 9 }, { 22, 10 }, { 0, 11 },
    { 0, 12 }, { 23, 13 }, { 0, 14 }, { 0, 15 } } };
// Act 2's exit pairs by side (0x6f112c, dx + 2 + 2 dy: up, left, -, right, down).
inline constexpr std::array<std::pair<int, int>, 5> kDesertExits = { { { 373, 372 }, { 372, 375 }, { 0, 0 }, { 373, 374 }, { 374, 375 } } };
// The desert's preset rows: FUN_0067f5c0's eight (0x6f2390, {def, file, x, y} x 5)
// and the canyon's nine (0x6f2610).
struct DesertPreset { int def, file, x, y; };
inline constexpr std::array<std::array<DesertPreset, 5>, 8> kDesertRows = { {
    { { { 376, 1, 0, 4 }, { 378, -1, 2, 4 }, { 377, -1, 4, 4 }, { 377, -1, 6, 4 }, { 376, 2, 8, 4 } } },
    { { { 376, 1, 0, 4 }, { 377, -1, 2, 4 }, { 378, -1, 4, 4 }, { 377, -1, 6, 4 }, { 376, 2, 8, 4 } } },
    { { { 376, 1, 0, 4 }, { 377, -1, 2, 4 }, { 377, -1, 4, 4 }, { 378, -1, 6, 4 }, { 376, 2, 8, 4 } } },
    { { { 376, 2, 8, 4 }, { 377, -1, 6, 4 }, { 382, -1, 4, 4 }, { 381, -1, 4, 6 }, { 379, 2, 4, 8 } } },
    { { { 376, 2, 8, 4 }, { 378, -1, 6, 4 }, { 382, -1, 4, 4 }, { 380, -1, 4, 6 }, { 379, 2, 4, 8 } } },
    { { { 379, 1, 4, 0 }, { 381, -1, 4, 2 }, { 380, -1, 4, 4 }, { 380, -1, 4, 6 }, { 379, 2, 4, 8 } } },
    { { { 379, 1, 4, 0 }, { 380, -1, 4, 2 }, { 381, -1, 4, 4 }, { 380, -1, 4, 6 }, { 379, 2, 4, 8 } } },
    { { { 379, 1, 4, 0 }, { 380, -1, 4, 2 }, { 380, -1, 4, 4 }, { 381, -1, 4, 6 }, { 379, 2, 4, 8 } } } } };
inline constexpr std::array<DesertPreset, 9> kCanyonRows = { { { 384, 0, 8, 0 }, { 383, 2, 6, 0 }, { 383, 1, 4, 0 }, { 383, 0, 2, 0 }, { 387, 0, 0, 0 },
                                                             { 385, 0, 0, 2 }, { 385, 1, 0, 4 }, { 385, 2, 0, 6 }, { 386, 0, 0, 8 } } };
// Corner row by index e (-40..40), 0x6f1088; 0 = none.
inline int corner_row(int edge) {
    static const std::unordered_map<int, int> rows = {
        { -40, 1 }, { -39, 9 }, { -38, 9 }, { -35, 1 }, { -34, 8 }, { -31, 12 }, { -26, 12 }, { -25, 4 },
        { -22, 5 }, { -21, 2 }, { -20, 2 }, { -19, 10 }, { -14, 10 }, { -13, 1 }, { -12, 9 }, { -11, 9 },
        { 11, 11 }, { 12, 11 }, { 13, 3 }, { 14, 12 }, { 19, 12 }, { 20, 4 }, { 21, 4 }, { 22, 7 },
        { 25, 2 }, { 26, 10 }, { 31, 10 }, { 34, 6 }, { 35, 3 }, { 38, 11 }, { 39, 11 }, { 40, 3 } };
    const auto found = rows.find(edge);
    return found == rows.end() ? 0 : found->second;
}
// Chaos Sanctuary's 25 presets of 24x24 (FUN_0067e840, 0x6f22c0).
inline constexpr std::array<int, 25> kChaosPresets = {
    836, 836, 836, 836, 836,
    836, 836, 861, 836, 836,
    836, 858, 862, 859, 836,
    836, 836, 860, 836, 836,
    836, 836, 857, 836, 836
};
// River Upper / Lower file by the border def already in the cell (0x6f2680).
inline constexpr std::array<std::array<int, 2>, 16> kRiver = { {
    { 0, 0 }, { 0, 6 }, { 0x182, 0 }, { 0, 8 }, { 2, 2 }, { 0, 3 }, { 1, 1 }, { 3, 0 },
    { 0, 2 }, { 0, 1 }, { 1, 0 }, { 2, 0 }, { 2, 3 }, { 1, 3 }, { 3, 1 }, { 3, 2 } } };
// Road floor sequence by the 8-neighbour mask (0x6f2700).
inline constexpr std::array<std::uint8_t, 256> kRoad = {
    0, 0, 16, 16, 0, 0, 16, 16, 14, 14, 6, 19, 14, 14, 6, 19, 15, 15, 5, 5, 15, 15, 21, 21, 8, 8, 10, 38, 8, 8, 40, 20,
    0, 0, 16, 16, 0, 0, 16, 16, 14, 14, 6, 19, 14, 14, 6, 19, 15, 15, 5, 5, 15, 15, 21, 21, 8, 8, 10, 38, 8, 8, 40, 20,
    13, 13, 7, 7, 13, 13, 13, 7, 4, 4, 11, 37, 4, 4, 11, 43, 3, 3, 12, 12, 3, 3, 39, 39, 9, 9, 2, 43, 9, 9, 44, 26,
    13, 13, 7, 7, 13, 13, 13, 7, 23, 23, 41, 17, 23, 23, 41, 17, 3, 3, 12, 12, 3, 3, 39, 39, 42, 42, 46, 42, 42, 42, 33, 31,
    0, 0, 16, 16, 0, 0, 16, 16, 14, 14, 6, 19, 14, 14, 6, 19, 15, 15, 5, 5, 15, 15, 21, 21, 8, 8, 10, 38, 8, 8, 35, 20,
    0, 0, 16, 16, 0, 0, 16, 16, 14, 14, 6, 19, 14, 14, 6, 19, 15, 15, 5, 5, 15, 15, 21, 21, 8, 8, 10, 38, 8, 8, 40, 20,
    13, 13, 7, 7, 13, 13, 13, 7, 4, 4, 11, 37, 4, 4, 11, 37, 18, 18, 35, 35, 18, 18, 22, 22, 36, 36, 45, 34, 36, 36, 28, 29,
    13, 13, 7, 7, 13, 13, 13, 7, 23, 23, 41, 17, 23, 23, 41, 17, 18, 18, 35, 35, 18, 18, 22, 22, 24, 24, 25, 32, 24, 24, 30, 1 };
// Goal direction (0..7) by 5·cdx + cdy + 12 (0x6f1518).
inline constexpr std::array<int, 25> kDirVal = { 5, 4, 4, 4, 3, 6, 5, 4, 3, 2, 6, 6, 6, 2, 2,
                                                 6, 7, 0, 1, 2, 7, 0, 0, 0, 1 };
// Path search turn rows (0x6f2840) and steps (0x6f2854 / 0x6f2850).
inline constexpr std::array<int, 17> kTurn = { 0, 1, 2, 3, 0, 1, 1, 1, 3, 2, 1, 2, 0, 3, 2, 1, 0 };
inline constexpr std::array<int, 4> kStepX = { 1, 0, -1, 0 }, kStepY = { 0, 1, 0, -1 };

inline int sgn(int value) { return (value > 0) - (value < 0); }
inline int sx2(int value) { return value + 2 * sgn(value); }

}  // namespace outdoor_detail

// The act 1 wilderness level `L` from its seed. The tiles come out
// level-relative: 4 wall layers, 2 floors, 1 shadow.
// Plain rooms' tiles get their DT1 variant later, as game.exe's rarity pick
// on the room seed (FUN_0066d820 in room_tiles.hpp level_room_tiles, into
// Level::picks): `diff_drlg.py 1-20 2 tiles` (and level 4) match.
// A plain room's DT1 mask by LevelType (FUN_006750f0): the wilderness
// 0x44103; the desert, Kurast, the Siege, Bloody Foothills 1; the jungle
// 4; Act 5's highlands 0x11.
constexpr std::uint32_t plain_dt1_mask(int level_type) {
    switch (level_type) {
        case 2: return 0x44103;
        case 0x10: case 0x16: case 0x1b: case 0x1c: return 1;
        case 0x15: return 4;
        case 0x1e: case 0x1f: return 0x11;
        default: return 0;
    }
}

Outdoor generate_outdoor(const OutdoorData& data, const OutdoorLevel& level, d2d::rules::Rng seed);

}  // namespace d2d::drlg
