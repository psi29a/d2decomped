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
inline d2d::rules::Rng level_seed(std::uint32_t map_seed, int level) {
    d2d::rules::Rng act{ map_seed };
    act.next();
    return d2d::rules::Rng{ act.low + std::uint32_t(level) };
}

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

// The game's cell grid: writes clip, reads index linearly like game.exe
// does (x one past a row reads the next row's first cell; off the end 0).
struct Cells {
    int width = 0, height = 0;
    std::vector<std::uint32_t> values;
    Cells(int width_, int height_) : width(width_), height(height_), values(std::size_t(width_) * std::size_t(height_), 0) {}
    [[nodiscard]] bool in(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
    [[nodiscard]] std::uint32_t get(int x, int y) const {
        const long index = long(y) * width + x;
        return index >= 0 && index < long(values.size()) ? values[std::size_t(index)] : 0;
    }
    // The operator table at 0x749bac.
    void op(int x, int y, std::uint32_t val, int op_code) {
        if (!in(x, y)) return;
        auto& cell = values[std::size_t(y) * std::size_t(width) + std::size_t(x)];
        switch (op_code) {
        case 0: cell |= val; break;
        case 1: cell &= val; break;
        case 2: cell ^= val; break;
        case 3: cell = val; break;
        case 4: if (!cell) cell = val; break;
        case 5: cell &= ~val; break;
        }
    }
};

struct Vert { int x = 0, y = 0; int style = 0; std::uint32_t flags = 0; int next = -1; };

struct Gen {
    const OutdoorData& data;
    const OutdoorLevel& level;
    d2d::rules::Rng seed;
    int cells_wide, cells_high;
    Cells g04, g18, g2c;
    std::uint32_t flags;
    std::vector<Vert> verts;
    int head = 0;
    struct Node { Placed rect; int side; bool town; int slot; };
    std::vector<Node> nodes;                            // +0x264, sorted
    std::unordered_map<int, int> rotation;              // per def, FUN_00674320
    std::vector<std::string>& notes;

    Gen(const OutdoorData& data_, const OutdoorLevel& level_in, d2d::rules::Rng rng, std::vector<std::string>& notes_out)
        : data(data_), level(level_in), seed(rng), cells_wide(level_in.rect.width / 8), cells_high(level_in.rect.height / 8), g04(cells_wide, cells_high), g18(cells_wide, cells_high), g2c(cells_wide, cells_high),
          flags(level_in.rect.flags), notes(notes_out) {}

    void note(std::string text) {
        if (std::ranges::find(notes, text) == notes.end()) notes.push_back(std::move(text));
    }
    [[nodiscard]] const Preset* preset(int def) const {
        const auto found = data.presets.find(def);
        return found == data.presets.end() ? nullptr : &found->second;
    }
    [[nodiscard]] std::pair<int, int> cells_of(int def) const {
        if (def == 0) return { 1, 1 };
        const auto* found = preset(def);
        return found ? std::pair{ found->width / 8, found->height / 8 } : std::pair{ 0, 0 };
    }

    // ---- presets on the cell grid (FUN_006743c0 / 00674230 / 00674320)
    int rotate(int def) {
        const auto* found = preset(def);
        if (!found || found->files < 1) return 0;
        auto rotation_found = rotation.find(def);
        if (rotation_found == rotation.end()) rotation_found = rotation.emplace(def, seed(found->files)).first;
        return rotation_found->second = (rotation_found->second + 1) % found->files;
    }
    void place(int x, int y, int def, int file, bool border) {
        if (def == 0) { g04.op(x, y, 0, 3); return; }   // LvlPrest's None is 0x0: only the def is written (act 5's empty rule cells)
        const auto [cells_x, cells_y] = cells_of(def);
        if (file == -1) file = rotate(def);
        for (int cell_y = y; cell_y < y + cells_y; ++cell_y)
            for (int cell_x = x; cell_x < x + cells_x; ++cell_x) {
                g2c.op(cell_x, cell_y, 0xf0000, 5);
                g2c.op(cell_x, cell_y, std::uint32_t(file) << 16 | 0x200, 0);
                if (border && ((def > 3 && def < 16) || unsigned(def - 0x16c) < 12)) g2c.op(cell_x, cell_y, 1, 0);
                g04.op(cell_x, cell_y, 0, 3);
            }
        g04.op(x, y, std::uint32_t(def), 3);
    }
    [[nodiscard]] bool free_cell(int x, int y) const { return (g2c.get(x, y) & 0x1b81) == 0; }
    [[nodiscard]] bool fits(int x, int y, int def, int match, int mask) const {
        auto [cells_x, cells_y] = cells_of(def);
        if (match) {
            if (mask & 1) { y -= match; cells_y += match; }
            if (mask & 2) cells_x += match;
            if (mask & 4) cells_y += match;
            if (mask & 8) { x -= match; cells_x += match; }
        }
        for (int cell_y = y; cell_y < y + cells_y; ++cell_y)
            for (int cell_x = x; cell_x < x + cells_x; ++cell_x)
                if (!g2c.in(cell_x, cell_y) || !free_cell(cell_x, cell_y)) return false;
        return true;
    }
    // n entries (i % w, i / w), then n swaps of two rolls.
    std::vector<std::pair<int, int>> shuffled(int width, int height) {
        const int count = width * height;
        std::vector<std::pair<int, int>> cells;
        for (int i = 0; i < count; ++i) cells.emplace_back(i % width, i / width);
        for (int k = 0; k < count; ++k) {
            const int first = seed(count), second = seed(count);
            std::swap(cells[std::size_t(first)], cells[std::size_t(second)]);
        }
        return cells;
    }
    bool anywhere(int def, int file, int match, int mask) {           // FUN_00674730
        for (auto [cell_x, cell_y] : shuffled(cells_wide - 2, cells_high - 2))
            if (fits(cell_x + 1, cell_y + 1, def, match, mask)) { place(cell_x + 1, cell_y + 1, def, file, false); return true; }
        return false;
    }
    bool by_road(int def, int file) {                              // FUN_00674920
        static constexpr std::array<int, 8> around_x = { -1, 0, 0, 1, -1, 1, 1, -1 }, around_y = { 0, -1, 1, 0, -1, 1, -1, 1 };
        for (auto [cell_x, cell_y] : shuffled(cells_wide - 2, cells_high - 2))
            if (g2c.get(cell_x + 1, cell_y + 1) & 0x80)
                for (int k = 0; k < 8; ++k)
                    if (fits(cell_x + 1 + around_x[std::size_t(k)], cell_y + 1 + around_y[std::size_t(k)], def, 0, 0xf)) {
                        place(cell_x + 1 + around_x[std::size_t(k)], cell_y + 1 + around_y[std::size_t(k)], def, file, false);
                        return true;
                    }
        return anywhere(def, file, 0, 0xf);
    }
    bool farthest(const Placed& from, int def, int file, int match, int mask) {   // FUN_006744f0
        const int start_x = seed(cells_wide - 2), start_y = seed(cells_high - 2);
        int best = 0, best_x = -1, best_y = -1;
        for (int i = 0; i <= cells_high - 2; ++i) {
            const int y = (i + start_y) % (cells_high - 2) + 1;
            for (int j = 0; j <= cells_wide - 2; ++j) {
                const int x = (j + start_x) % (cells_wide - 2) + 1;
                if (!fits(x, y, def, match, mask)) continue;
                const int dx = std::abs(x * 8 - (from.width / 2 + from.x) + 4 + level.rect.x);
                const int dy = std::abs(y * 8 - (from.height / 2 + from.y) + 4 + level.rect.y);
                const int distance = (dy < dx ? dy + 2 * dx : dx + 2 * dy) / 2;
                if (best < distance) { best = distance; best_x = x; best_y = y; }
            }
        }
        if (best_x == -1) return false;
        place(best_x, best_y, def, file, false);
        return true;
    }

    // ---- outline (FUN_00677680, FUN_0067d050, FUN_0067ce20, FUN_00675080)
    void neighbours() {
        const Placed& rect = level.rect;
        for (const auto& neighbour : level.neighbours) {
            const Placed& neighbour_rect = neighbour.rect;
            int side = -1;                                          // FUN_00642240
            if (neighbour_rect.x < rect.x) { if (rect.x == neighbour_rect.x + neighbour_rect.width) side = 0; }
            else if (neighbour_rect.x == rect.x + rect.width) side = 2;
            if (side < 0) {
                if (neighbour_rect.y < rect.y) { if (rect.y == neighbour_rect.y + neighbour_rect.height) side = 1; }
                else if (neighbour_rect.y == rect.y + rect.height) side = 3;
            }
            if (side < 0) { note("drlg: neighbour " + std::to_string(neighbour_rect.level) + " doesn't touch"); continue; }
            const Node node{ neighbour_rect, side, neighbour.preset, neighbour.slot };
            auto before = [](const Node& first, const Node& second) {        // FUN_0066b6a0
                if (first.side != second.side) return first.side < second.side;
                switch (first.side) {
                case 0: return first.rect.y < second.rect.y;
                case 1: return second.rect.x < first.rect.x;
                case 2: return second.rect.y < first.rect.y;
                default: return first.rect.x < second.rect.x;
                }
            };
            // FUN_0066b720: the head is only compared while it's alone.
            if (nodes.empty()) nodes.push_back(node);
            else if (nodes.size() == 1) nodes.insert(before(node, nodes[0]) ? nodes.begin() : nodes.end(), node);
            else {
                auto found = nodes.begin() + 1;
                while (found != nodes.end() && !before(node, *found)) ++found;
                nodes.insert(found, node);
            }
        }
    }
    int add_vert(int after, int x, int y) {
        verts.push_back({ x, y, 0, 0, verts[std::size_t(after)].next });
        verts[std::size_t(after)].next = int(verts.size()) - 1;
        return int(verts.size()) - 1;
    }
    void outline() {
        const int x = level.rect.x, y = level.rect.y, last_x = level.rect.width - 1, last_y = level.rect.height - 1;
        verts = { { x, y + last_y, 0, 0, 1 }, { x, y, 0, 0, 2 }, { x + last_x, y, 0, 0, 3 }, { x + last_x, y + last_y, 0, 0, 0 } };
        head = 0;
        for (const auto& node : nodes) {
            const int node_x = node.rect.x, node_y = node.rect.y, node_last_x = node.rect.width - 1, node_last_y = node.rect.height - 1;
            int low, high, sign, edge_start, edge_end, corner;
            bool vertical;
            switch (node.side) {
            case 0: low = node_y; high = node_y + node_last_y; sign = -1; edge_start = verts[0].y; edge_end = verts[1].y; corner = 0; vertical = true; break;
            case 1: high = node_x; low = node_x + node_last_x; sign = 1; edge_start = verts[1].x; edge_end = verts[2].x; corner = 1; vertical = false; break;
            case 2: high = node_y; low = node_y + node_last_y; sign = 1; edge_start = verts[2].y; edge_end = verts[3].y; corner = 2; vertical = true; break;
            default: low = node_x; high = node_x + node_last_x; sign = -1; edge_start = verts[3].x; edge_end = verts[0].x; corner = 3; vertical = false; break;
            }
            // In c-space the edge runs s -> e; the neighbour spans na (its
            // near end, `hi`) to nb (`lo`).
            auto split_at = [&](int after, int coord) {
                const auto& vert = verts[std::size_t(after)];
                return vertical ? add_vert(after, vert.x, coord) : add_vert(after, coord, vert.y);
            };
            auto mark = [&](int vert) { verts[std::size_t(vert)].flags |= 1 | (node.town ? 2u : 0u); };
            const int near_end = high * sign, far_end = low * sign;
            int vert = corner;
            if (near_end <= edge_start * sign) {
                if (edge_start * sign <= far_end) {
                    mark(vert);
                    if (far_end < edge_end * sign) split_at(vert, low);
                }
            } else if (near_end <= edge_end * sign) {
                vert = split_at(vert, high);
                mark(vert);
                if (far_end < edge_end * sign) split_at(vert, low);
            }
        }
        // Level-relative cells, equal neighbours merged.
        int vert_index = head;
        do {
            auto& vert = verts[std::size_t(vert_index)];
            vert.x = (vert.x - level.rect.x) / 8;
            vert.y = (vert.y - level.rect.y) / 8;
            vert_index = vert.next;
        } while (vert_index != head);
        vert_index = head;
        do {
            auto& vert = verts[std::size_t(vert_index)];
            const int next = vert.next;
            const auto& next_vert = verts[std::size_t(next)];
            if (vert.x == next_vert.x && vert.y == next_vert.y && next != vert_index) {
                if (next == head) head = vert_index;
                vert.next = next_vert.next;
                vert.flags |= next_vert.flags;
                vert.style = next_vert.style;
            }
            vert_index = verts[std::size_t(vert_index)].next;
        } while (vert_index != head);
    }

    // ---- contact spans and borders (FUN_00675770, FUN_00675850)
    [[nodiscard]] std::uint32_t contact_bit(const Vert& vert) const {         // FUN_00674040
        int side;
        if (vert.x == 0) side = vert.y == 0 ? 1 : 0;
        else if (vert.y == 0) side = vert.x == cells_wide - 1 ? 2 : 1;
        else if (vert.x == cells_wide - 1) side = vert.y == cells_high - 1 ? 3 : 2;
        else if (vert.y == cells_high - 1) side = 3;
        else return 0;
        static constexpr std::array<int, 4> probe_x = { -4, 4, 12, 4 }, probe_y = { 4, -4, 4, 12 };
        const int probe_x_at = level.rect.x + vert.x * 8 + probe_x[std::size_t(side)], probe_y_at = level.rect.y + vert.y * 8 + probe_y[std::size_t(side)];
        for (const auto& node : nodes)
            if (node.side == side && probe_x_at >= node.rect.x && probe_x_at < node.rect.x + node.rect.width && probe_y_at >= node.rect.y && probe_y_at < node.rect.y + node.rect.height)
                return 1u << (node.slot + 4);
        return 0;
    }
    // FUN_0067c760 walks the cells strictly between v and n, then writes v,
    // then n (its last argument, 1 here): both ends inclusive either way.
    void span(const Vert& vert, const Vert& next, Cells& cells, std::uint32_t val) {
        for (int y = std::min(vert.y, next.y); y <= std::max(vert.y, next.y); ++y)
            for (int x = std::min(vert.x, next.x); x <= std::max(vert.x, next.x); ++x) cells.op(x, y, val, 0);
    }
    void contacts() {
        int vert_index = head;
        do {
            const auto& vert = verts[std::size_t(vert_index)];
            if (vert.flags & 1) {
                const auto& next = verts[std::size_t(vert.next)];
                span(vert, next, g18, contact_bit(vert));
                span(vert, next, g2c, vert.style ? 3 : 1);
            }
            vert_index = vert.next;
        } while (vert_index != head);
    }
    void borders() {
        int vert_index = head;
        do {
            const auto& first = verts[std::size_t(vert_index)];
            const auto& second = verts[std::size_t(first.next)];
            const auto& third = verts[std::size_t(second.next)];
            const int dx1 = sgn(second.x - first.x), dy1 = sgn(second.y - first.y), dx2 = sgn(third.x - second.x), dy2 = sgn(third.y - second.y);
            const int len = dx1 == 0 ? std::abs(first.y - second.y) : std::abs(first.x - second.x);
            std::uint32_t bits = first.style ? 3 : 1;
            // Edge: kind left 0, up 1, right 2, down 3 (0x6f0fd0).
            const int kind = dx1 == -1 ? 0 : dy1 == -1 ? 1 : dx1 == 1 ? 2 : 3;
            const int edge = is_desert() ? 0x16c + kind : is_mesa() ? 799 + kind : kBorder[std::size_t(kind + 1)][first.style == 0 ? 1 : 0];   // desert col 2 (364..375), mesa col 3 (799..810)
            if (!(first.flags & 2)) {
                int x = first.x, y = first.y;
                while (x != second.x || y != second.y) {
                    y += dy1;
                    x += dx1;
                    place(x, y, edge, -1, false);
                    g2c.op(x, y, bits, 0);
                }
            }
            if ((first.flags & 1) && !(first.flags & 2)) {                             // the opening
                const int edge_x = std::min(first.x, second.x) + len * std::abs(dx1) / 2;
                const int edge_y = std::min(first.y, second.y) + len * std::abs(dy1) / 2;
                if (is_desert()) {                                                      // act 2: a pair of presets (0x6f112c by dx + 2 + 2 dy)
                    const auto& pair = kDesertExits[std::size_t(std::clamp(dx1 + 2 + dy1 * 2, 0, 4))];   // an edge runs along one axis: 0..4
                    place(edge_x, edge_y, pair.first, -1, false);
                    place(edge_x + std::abs(dx1), edge_y + std::abs(dy1), pair.second, -1, false);
                } else {
                    g2c.op(edge_x, edge_y, 0xf0000, 5);
                    g2c.op(edge_x, edge_y, level.rect.level == level_ids::kBurialGrounds ? 0x40400 : 0x30400, 0);
                }
            }
            const int style = first.style ? first.style : second.style;
            if (style) bits |= 2;
            int edge_index;
            if (!(first.flags & 2)) edge_index = !(second.flags & 2) ? sx2(2 * dx1) + 9 * (sx2(2 * dx2) + 2 * dy2) + 2 * dy1
                                                   : sx2(2 * dx1) + 9 * (sx2(dx2) + dy2) + 2 * dy1;
            else edge_index = !(second.flags & 2) ? sx2(dx1) + 9 * (sx2(2 * dx2) + 2 * dy2) + dy1
                                    : sx2(dx1) + 9 * (sx2(dx2) + dy2) + dy1;
            if (const int row = corner_row(edge_index)) {
                int def = is_desert() ? 0x16b + row : is_mesa() ? 798 + row : kBorder[std::size_t(row)][style == 0 ? 1 : 0];
                if (def == 19) def = first.style == 1 ? 19 + (second.style != 1) : 21;
                if (def) {
                    place(second.x, second.y, def, -1, false);
                    g2c.op(second.x, second.y, bits, 0);
                }
            }
            vert_index = first.next;
        } while (vert_index != head);
        // ponytail: FUN_00675670 (blanking outside a non-rectangular
        // outline) skipped — every level here is a rectangle.
    }

    // ---- border substitution (FUN_006752a0 -> FUN_00670750)
    static std::uint32_t word(const d2d::ds1::Map& map, const std::vector<d2d::ds1::Layer>& layers, int x, int y) {
        if (layers.empty() || x < 0 || y < 0 || x >= map.width() || y >= map.height()) return 0;
        return d2d::ds1::Map::word(layers[0].cells[std::size_t(y) * std::size_t(map.width()) + std::size_t(x)]);
    }
    // Act 5's rule cells (FUN_0067e000, 0x6f2038): a wall's style 48 / 49
    // and seq give the def it stands for — {style, first seq, last seq, def,
    // the Frozen Tundra's def}; 0 an empty cell, -5 any.
    [[nodiscard]] int rule_def(std::uint32_t wall_word) const {
        struct Rule { int style, first, last, def, tundra_def; };
        static constexpr std::array<Rule, 10> kRules = { { { 49, 1, 16, 915, 987 }, { 49, 31, 46, 915, 987 }, { 48, 1, 1, 883, 959 }, { 48, 2, 3, 881, 957 },
                                                          { 48, 4, 4, 884, 960 }, { 48, 5, 5, 895, 971 }, { 48, 6, 7, 893, 969 }, { 48, 8, 8, 896, 972 },
                                                          { 48, 30, 30, 0, 0 }, { 48, 31, 31, -5, -5 } } };
        const int style = int((wall_word >> 20) & 0x3f), seq = int((wall_word >> 8) & 0xff);
        for (const auto& rule : kRules)
            if (rule.style == style && seq >= rule.first && seq <= rule.last)
                return (level.rect.level == level_ids::kFrozenTundra ? rule.tundra_def : rule.def) - rule.first + seq;
        return -6;                                                             // game.exe asserts (OutSiege.cpp 0x184 / 0x1a5)
    }
    // FUN_0066f3b0: the group fits at (x, y), snapped to the row's grid; a
    // cell per DS1 tile, `grid` cells apart. Act 5's rules (`rules`) match
    // every tile by its def (FUN_0067e080).
    bool sub_match(const d2d::ds1::Map& map, const d2d::ds1::Group& group, int x, int y, int grid = 1, bool rules = false) {
        x -= x % grid;
        y -= y % grid;
        for (int group_y = 0; group_y < group.height; ++group_y)
            for (int group_x = 0; group_x < group.width; ++group_x) {
                const auto wall_word = word(map, map.walls(), group.x + group_x, group.y + group_y), floor_word = word(map, map.floors(), group.x + group_x, group.y + group_y);
                const int cell_x = x + grid * group_x, cell_y = y + grid * group_y;
                if (rules) {
                    const int def = rule_def(wall_word);
                    if (def == -5) continue;
                    if (def != int(g04.get(cell_x, cell_y)) || (g2c.get(cell_x, cell_y) & 0x400)) return false;
                } else if (wall_word & 1) {
                    const int style = int((wall_word >> 8) & 0xff) - 1;
                    if (style != 62 && std::uint32_t(border_base() + style) != g04.get(cell_x, cell_y)) return false;
                    if (g2c.get(cell_x, cell_y) & 0x400) return false;
                } else if (floor_word & 2) {
                    if (!g2c.in(cell_x, cell_y) || !free_cell(cell_x, cell_y)) return false;
                }
            }
        return true;
    }
    void sub_apply(const d2d::ds1::Map& map, const d2d::ds1::Group& group, int x, int y, int xoff, int grid = 1, bool rules = false) {   // FUN_0066f520
        x -= x % grid;
        y -= y % grid;
        for (int group_y = 0; group_y < group.height; ++group_y)
            for (int group_x = 0; group_x < group.width; ++group_x) {
                const auto wall_word = word(map, map.walls(), group.x + xoff + group_x, group.y + group_y);
                const auto floor_word = word(map, map.floors(), group.x + xoff + group_x, group.y + group_y);
                const int cell_x = x + grid * group_x, cell_y = y + grid * group_y;
                if (wall_word & 1) {
                    const int style = int((wall_word >> 8) & 0xff) - 1;
                    const int def = rules ? rule_def(wall_word) : border_base() + style;
                    if (def != -5 && style != 62) place(cell_x, cell_y, def, 0, true);
                } else if (floor_word & 2) {
                    g04.op(cell_x, cell_y, 0, 3);
                    g2c.op(cell_x, cell_y, 0, 3);
                } else {
                    g04.op(cell_x, cell_y, 0, 3);
                    g2c.op(cell_x, cell_y, 0x100, 3);
                }
            }
    }
    bool sub_group(const Sub& sub, const d2d::ds1::Group& group, int type) {       // FUN_0066f690
        const bool act1 = level.rect.level >= level_ids::kBloodMoor && level.rect.level <= level_ids::kTamoeHighland;
        const bool rules = type == 12;                                            // act 5's barricade rules (FUN_0067e0e0)
        const int margin = type == 1 && (flags & 0xc) ? -1 : 1;
        const int spots_x = cells_wide - group.width * sub.grid_size + margin, spots_y = cells_high - group.height * sub.grid_size + 1;
        const int spots = spots_x * spots_y;
        if (spots <= 0) return false;
        const bool skip22 = type == 1 && act1 && spots_x <= 5 && spots_y <= 5;
        for (auto [spot_x, spot_y] : shuffled(spots_x, spots_y)) {
            if (skip22 && spot_x == 2 && spot_y == 2) continue;
            if (!sub_match(*sub.map, group, spot_x, spot_y, sub.grid_size, rules)) continue;
            const int variant = seed(group.variants);
            sub_apply(*sub.map, group, spot_x, spot_y, (group.width + 1) * (variant + 1), sub.grid_size, rules);
            if (sub.bord_type == 0 || sub.bord_type == 1) return true;
        }
        return false;
    }
    void border_subs(int type) {                                               // FUN_006752a0(type, the border base)
        for (const auto& sub : data.subs) {
            if (sub.type != type || !sub.map) continue;
            const auto& groups = sub.map->groups();
            const int count = int(groups.size());
            if (count == 0) continue;
            const int start = sub.bord_type == 0 ? seed(count) : 0;
            for (int i = 0; i < count; ++i)
                if (sub_group(sub, groups[std::size_t((start + i) % count)], type) && sub.bord_type == 0) break;
        }
    }

    // ---- river, entrances, transitions (FUN_00680200, FUN_006803d0)
    [[nodiscard]] bool river_ok(int x) const {                                 // FUN_0067fc70
        for (int y = 0; y < cells_high; ++y)
            if ((g2c.get(x, y) & 2) || (g2c.get(x + 1, y) & 2)) return false;
        return true;
    }
    void river(int x) {                                                        // FUN_0067fe90
        auto file = [&](int cell_x, int cell_y, int half) {
            const auto cell_flags = g2c.get(cell_x, cell_y), def = g04.get(cell_x, cell_y);
            if (def == 0) return (cell_flags & 0x100) ? 0 : 3;
            if (def == 7 && (cell_flags & 0xf0000) == 0x30000) return 3;
            return def < kRiver.size() ? kRiver[def][std::size_t(half)] : 0;
        };
        for (int y = 0; y < cells_high; ++y) {
            place(x, y, 26, file(x, y, 0), false);
            place(x + 1, y, 27, file(x + 1, y, 1), false);
        }
        if (!(flags & 0x14)) return;
        const int count = cells_high - 2;                                                  // FUN_0067fd20
        const int row_pick = seed(count);
        for (int i = 0; i < count; ++i) {
            const int y = (i + row_pick) % count + 1;
            if (!free_cell(x - 1, y)) continue;
            if (!(flags & 4) && !free_cell(x + 2, y)) continue;                  // flags&4 jumps past the x+2 setup
            if ((g2c.get(x, y) & 0xf0000) != 0x30000 || (g2c.get(x + 1, y) & 0xf0000) != 0x30000) continue;
            place(x, y, 28, 1, false);
            place(x + 1, y, 28, 2 + ((flags & 4) ? 1 : 0), false);
            return;
        }
    }
    // Cliff styles (FUN_00680070): a run of edges going right or up from a
    // vertex where the outline turns, none on a contact span (flag 1),
    // up to its last turn, becomes cliffs (style 1).
    void cliffs() {
        auto next = [&](int vert) { return verts[std::size_t(vert)].next; };
        auto vert_x = [&](int vert) { return verts[std::size_t(vert)].x; };
        auto vert_y = [&](int vert) { return verts[std::size_t(vert)].y; };
        auto contact = [&](int vert) { return (verts[std::size_t(vert)].flags & 1) != 0; };
        int prev = head;
        for (int vert = next(head); vert != head; vert = next(vert)) prev = vert;
        int cur = head, start = head;
        bool wrapped = false;
        for (;;) {
            int walk = cur;
            const int after = next(cur);
            if (((vert_x(cur) < vert_x(after) && vert_y(cur) < vert_y(prev)) || (vert_y(after) < vert_y(cur) && vert_x(cur) < vert_x(prev))) && !contact(cur) && !contact(prev)) {
                int last = -1, following;
                do {
                    if (walk == start) wrapped = true;
                    following = next(walk);
                    if (vert_y(walk) < vert_y(following) || vert_x(following) < vert_x(walk) || contact(walk) || contact(following)) break;
                    const int beyond = next(following);
                    if (!contact(following) && ((vert_x(walk) < vert_x(following) && vert_y(following) < vert_y(beyond)) || (vert_y(following) < vert_y(walk) && vert_x(following) < vert_x(beyond)))) last = walk;
                    walk = following;
                } while (following != cur);
                if (last != -1) {
                    for (int vert = cur; vert != last; vert = next(vert)) verts[std::size_t(vert)].style = 1;
                    verts[std::size_t(last)].style = 1;
                    flags |= 0x20;
                }
            }
            cur = next(walk);
            if (wrapped) return;
            prev = walk;
            start = head;
            if (cur == head) return;
        }
    }
    // A cliff cave (FUN_006801a0) on the first cliff edge cell: rows first
    // on an even roll, else columns (x bounded by the height, as game.exe).
    bool cliff_cave(int x, int y) {
        const auto def = g04.get(x, y);
        if (def != 16 && def != 17) return false;
        place(x, y, def == 16 ? 25 : 24, -1, false);
        flags |= 0x40;
        return true;
    }
    void features() {
        if ((flags & 0xc) && river_ok(cells_wide - 2)) river(cells_wide - 2);
        if ((flags & 0x20) && !(flags & 0x40)) {
            bool found = false;
            if (!(seed.next() & 1)) {
                for (int y = 0; y < cells_high && !found; ++y)
                    for (int x = 0; x < cells_wide && !found; ++x) found = cliff_cave(x, y);
            } else {
                for (int x = 0; x < cells_high && !found; ++x)
                    for (int y = 0; y < cells_wide && !found; ++y) found = cliff_cave(x, y);
            }
            if (!found) note("drlg: no cliff for the cliff cave (game.exe stops here)");
        }
        if ((flags & 0x1c) && !(flags & 0x40)) {
            const int pick = int(seed.next() & 3);
            const int x = (pick & 1) ? 3 : cells_wide - ((flags & 0x10) ? 4 : 5);
            const int y = (pick >> 1) ? 3 : cells_high - 4;
            place(x, y, 51 + (level.rect.level == level_ids::kBloodMoor), -1, false);
            flags |= 0x40;
        }
    }
    void transitions() {
        if ((flags & 0x10) && river_ok(cells_wide / 2 - 1)) river(cells_wide / 2 - 1);
        if (flags & 0x80) place(0, 0, 3, 1, false);
        if (flags & 0x100) place(cells_wide - 7, 0, 3, 2, false);
        if (flags & 0x200) place(0, 1, 2, 1, false);
        if (flags & 0x400) place(0, cells_high - 6, 2, 1, false);
        if (!(flags & 0x40)) {
            const bool ok = level.rect.level == level_ids::kBloodMoor ? farthest(level.town, 52, -1, 1, 0xf) : anywhere(51, -1, 1, 0xf);
            if (!ok) note("drlg: no room for the level's cave entrance");
            flags |= 0x40;
        }
    }

    // ---- roads (FUN_00681420)
    struct End { int x = 0, y = 0, dir = 4; };
    std::array<End, 6> end_points{}, road_starts{}, road_goals{}, hub_points{};
    int ends = 0;
    [[nodiscard]] End snap(End point) const {                                      // FUN_00680cc0
        int rel_x = point.x - level.rect.x, rel_y = point.y - level.rect.y;
        switch (point.dir) {
        case 0: rel_x = rel_x / 8 * 8 + 11; break;
        case 1: rel_y = rel_y / 8 * 8 + 11; break;
        case 2: rel_x = rel_x / 8 * 8 - 5; break;
        case 3: rel_y = rel_y / 8 * 8 - 5; break;
        default: break;
        }
        return { rel_x + level.rect.x, rel_y + level.rect.y, point.dir };
    }
    void road_ends() {                                                         // FUN_00680d70
        auto add = [&](End end) { if (ends < 6) end_points[std::size_t(ends)] = end; ++ends; };
        for (const auto& node : nodes) {
            const auto& rect = node.rect;
            if (rect.level == level_ids::kRogueEncampment) {
                static constexpr std::array<std::pair<int, int>, 4> town_exit = { { { 59, 19 }, { 29, 35 }, { 4, 22 }, { 29, 3 } } };
                add({ rect.x + town_exit[std::size_t(node.side)].first, rect.y + town_exit[std::size_t(node.side)].second, node.side });
            } else if (rect.level == level_ids::kMonasteryGate) add({ rect.x + 27, rect.y + 13, 1 });
        }
        for (int x = 0; x < cells_wide; ++x)
            for (int y = 0; y < cells_high; ++y) {
                const auto def = g04.get(x, y);
                const auto cell_flags = (g2c.get(x, y) >> 16) & 0xf;
                int dir = 4;
                switch (def) {
                case 4: if (cell_flags == 3) dir = 3; break;
                case 5: if (cell_flags == 3) dir = 0; break;
                case 6: if (cell_flags == 3) dir = 1; break;
                case 7: if (cell_flags == 3) dir = 2; break;
                case 24: dir = 1; break;
                case 25: dir = 0; break;
                case 28: if (cell_flags == 1 && x == cells_wide - 2) dir = 2; break;
                case 51: case 52: dir = cell_flags != 0; break;
                default: break;
                }
                if (dir != 4) add({ level.rect.x + 3 + x * 8, level.rect.y + 3 + y * 8, dir });
            }
        if (ends > 6) { note("drlg: more than 6 road ends"); ends = 6; }
        for (int i = 0; i < ends; ++i) road_starts[std::size_t(i)] = snap(end_points[std::size_t(i)]);
    }
    void road_hub() {                                                          // FUN_00681000
        if (flags & 0x10) {
            for (int y = 0; y < cells_high; ++y)
                for (int x = 1; x < cells_wide - 1; ++x)
                    if (g04.get(x, y) == 28 && ((g2c.get(x, y) >> 16) & 0xf) == 1) {
                        const int hub_x = level.rect.x + 3 + x * 8;
                        for (int i = 0; i < ends; ++i) {
                            auto& hub = hub_points[std::size_t(i)];
                            hub.y = level.rect.y + 3 + y * 8;
                            if (hub_x < end_points[std::size_t(i)].x) { hub.x = hub_x + 8; hub.dir = 0; }
                            else { hub.x = hub_x; hub.dir = 2; }
                            road_goals[std::size_t(i)] = snap(hub);
                        }
                        return;
                    }
        }
        int center_x, center_y;
        if (ends == 1) { center_x = cells_wide / 2; center_y = cells_high / 2; }
        else {
            int sum_x = 0, sum_y = 0;
            for (int i = 0; i < ends; ++i) { sum_x += end_points[std::size_t(i)].x - level.rect.x; sum_y += end_points[std::size_t(i)].y - level.rect.y; }
            center_x = ends ? sum_x / (ends * 8) : 0;
            center_y = ends ? sum_y / (ends * 8) : 0;
        }
        static constexpr std::array<int, 4> ring_x = { -1, 0, 0, 1 }, ring_y = { 0, 1, -1, 0 };
        int hub_x = center_x, hub_y = center_y;
        bool found = false;
        for (int ring = 0; ring < 8 && !found; ++ring)
            for (int k = 0; k < 4; ++k) {
                hub_x = ring_x[std::size_t(k)] * ring + center_x;
                hub_y = ring_y[std::size_t(k)] * ring + center_y;
                if (g2c.in(hub_x, hub_y) && free_cell(hub_x, hub_y)) { found = true; break; }
            }
        for (int i = 0; i < ends; ++i) {
            hub_points[std::size_t(i)] = { level.rect.x + 3 + hub_x * 8, level.rect.y + 3 + hub_y * 8, 4 };
            road_goals[std::size_t(i)] = snap(hub_points[std::size_t(i)]);
        }
    }
    static int dirval(int x, int y, int goal_x, int goal_y) {                           // FUN_00678cf0
        const int dx = goal_x - x, dy = goal_y - y, abs_x = std::abs(dx), abs_y = std::abs(dy);
        auto squash = [](int value) { return value < 0 ? -1 : (value & 1); };
        int clamped_x = std::clamp(dx, -2, 2), clamped_y = std::clamp(dy, -2, 2);
        if (abs_x >= 2 * abs_y) clamped_y = squash(dy);
        else if (abs_y >= 2 * abs_x) clamped_x = squash(dx);
        return kDirVal[std::size_t(5 * clamped_x + clamped_y + 12)];
    }
    // Cells start -> goal, goal first; empty when there's no way.
    std::vector<std::pair<int, int>> search(int start_x, int start_y, int goal_x, int goal_y) {   // FUN_006817d0 / 00681630
        auto hdist = [](int delta_x, int delta_y) { delta_x = std::abs(delta_x); delta_y = std::abs(delta_y); return std::min(delta_x, delta_y) + 2 * std::max(delta_x, delta_y); };
        if (std::abs(start_x - goal_x) + std::abs(start_y - goal_y) < 2) return { { start_x, start_y }, { goal_x, goal_y } };
        struct N { int estimate, heuristic, cost, x, y, tries, pool_next, dir, parent, child; };
        std::vector<N> pool(900);
        const int first_heuristic = hdist(start_x - goal_x, start_y - goal_y);
        int bound = first_heuristic / 2 + first_heuristic;
        const int limit = bound + 35;
        do {
            int count = 1;
            pool[0] = { first_heuristic, first_heuristic, 0, start_x, start_y, -1, 0, (dirval(start_x, start_y, goal_x, goal_y) / 2) & 3, -1, -1 };
            auto advance = [&](int index) -> int {                                  // FUN_00681560
                auto& node = pool[std::size_t(index)];
                if (node.tries < 4) { ++node.pool_next; node.dir = (node.dir + kTurn[std::size_t(node.pool_next)]) & 3; }
                if (++node.tries != 3) return index;
                while (true) {
                    if (index == 0) return -1;
                    index = pool[std::size_t(index)].parent;
                    auto& next = pool[std::size_t(index)];
                    ++next.pool_next;
                    next.dir = (next.dir + kTurn[std::size_t(next.pool_next)]) & 3;
                    if (++next.tries != 3) return index;
                }
            };
            int found = -1, cur = 0;
            while (cur >= 0) {
                const auto& node = pool[std::size_t(cur)];
                if (node.x == goal_x && node.y == goal_y) { found = cur; break; }
                const int next_x = node.x + kStepX[std::size_t(node.dir)], next_y = node.y + kStepY[std::size_t(node.dir)];
                bool ok = next_x == goal_x && next_y == goal_y;
                if (!ok && g2c.in(next_x, next_y) && !(g2c.get(next_x, next_y) & 0x200)) {
                    ok = true;
                    for (int step = cur; step >= 0; step = pool[std::size_t(step)].parent)
                        if (pool[std::size_t(step)].x == next_x && pool[std::size_t(step)].y == next_y) { ok = false; break; }
                }
                if (!ok) { cur = advance(cur); continue; }
                const int next_cost = node.cost + 2, heuristic = hdist(next_x - goal_x, next_y - goal_y), estimate = heuristic + next_cost;
                if (estimate > bound) { cur = advance(cur); continue; }
                if (node.child < 0) {                             // a reused child keeps its own
                    if (count == 900) { cur = -1; break; }
                    pool[std::size_t(count)] = { 0, 0, 0, 0, 0, 0, 0, 0, cur, -1 };
                    pool[std::size_t(cur)].child = count++;
                }
                const int child_index = pool[std::size_t(cur)].child;
                auto& child_node = pool[std::size_t(child_index)];
                child_node.parent = cur;
                child_node.heuristic = heuristic; child_node.estimate = estimate; child_node.cost = next_cost; child_node.tries = 0;
                const int dir_value = dirval(next_x, next_y, goal_x, goal_y) / 2;
                child_node.pool_next = ((pool[std::size_t(cur)].dir - dir_value) & 3) * 4;
                child_node.dir = (kTurn[std::size_t(child_node.pool_next)] + dir_value) & 3;
                child_node.x = next_x; child_node.y = next_y;
                cur = child_index;
            }
            bound += 5;
            if (count > 899) return {};
            if (found >= 0) {
                std::vector<std::pair<int, int>> path;
                for (int step = found; step >= 0; step = pool[std::size_t(step)].parent) path.emplace_back(pool[std::size_t(step)].x, pool[std::size_t(step)].y);
                return path;
            }
        } while (bound < limit);
        return {};
    }
    std::vector<std::vector<std::pair<int, int>>> roads() {
        road_ends();
        road_hub();
        std::vector<std::vector<std::pair<int, int>>> out;
        for (int i = 0; i < ends; ++i) {
            const auto& start = road_starts[std::size_t(i)];
            const auto& goal = road_goals[std::size_t(i)];
            auto path = search((start.x - level.rect.x) / 8, (start.y - level.rect.y) / 8, (goal.x - level.rect.x) / 8, (goal.y - level.rect.y) / 8);
            if (path.empty()) { note("drlg: a road found no way"); continue; }
            for (auto [cell_x, cell_y] : path) if (g2c.in(cell_x, cell_y)) g2c.op(cell_x, cell_y, 0x80, 0);
            // FUN_00681240: goal := C, start := B, the rest jittered, A
            // after B, D before C unless it's the plain hub.
            static constexpr std::array<int, 4> jitter_x = { 1, 0, -1, 0 }, jitter_y = { 0, 1, 0, -1 };
            int jitter = int(seed.next() & 3);
            std::vector<std::pair<int, int>> line;
            if (hub_points[std::size_t(i)].dir != 4) line.emplace_back(hub_points[std::size_t(i)].x, hub_points[std::size_t(i)].y);
            line.emplace_back(goal.x, goal.y);
            for (std::size_t j = 1; j + 1 < path.size(); ++j) {
                const auto roll_x = seed.next();
                const auto roll_y = seed.next();
                line.emplace_back(level.rect.x + path[j].first * 8 + 3 + int((roll_x & 1) + 2) * jitter_x[std::size_t(jitter)],
                                  level.rect.y + path[j].second * 8 + 3 + int((roll_y & 1) + 2) * jitter_y[std::size_t(jitter)]);
                jitter = (jitter + 1) & 3;
            }
            line.emplace_back(start.x, start.y);
            line.emplace_back(end_points[std::size_t(i)].x, end_points[std::size_t(i)].y);
            out.push_back(std::move(line));
        }
        return out;
    }

    // ---- shrines, waypoints, fills (FUN_00674e40, FUN_00674b70, FUN_00680580)
    void shrines(int count) {
        int corner = int(seed.next() & 3);
        for (auto [cell_x, cell_y] : shuffled(cells_wide - 2, cells_high - 2)) {
            if (count < 1) return;
            if (!free_cell(cell_x + 1, cell_y + 1)) continue;
            g18.op(cell_x + 1, cell_y + 1, 0x1000u << corner, 0);
            g2c.op(cell_x + 1, cell_y + 1, 0x1000, 0);
            corner = (corner + 1) & 3;
            --count;
        }
    }
    void waypoint() {
        if (level.rect.level == level_ids::kColdPlains) {                                            // by the Blood Moor's exit
            int slot = 8;
            for (const auto& neighbour : level.neighbours) if (neighbour.rect.level == level_ids::kBloodMoor) slot = neighbour.slot;
            const std::uint32_t bit = slot < 8 ? 1u << (slot + 4) : 0;
            for (int y = 0; y < cells_high; ++y)
                for (int x = 0; x < cells_wide; ++x) {
                    if (!(g18.get(x, y) & bit) || !(g2c.get(x, y) & 0x400)) continue;
                    const int way_x = std::clamp(x, 1, cells_wide - 2), way_y = std::clamp(y, 1, cells_high - 2);
                    g18.op(way_x, way_y, 0x20000, 0);
                    g2c.op(way_x, way_y, 0x800, 0);
                    return;
                }
        }
        for (auto [cell_x, cell_y] : shuffled(cells_wide - 2, cells_high - 2)) {
            if (!free_cell(cell_x + 1, cell_y + 1)) continue;
            g18.op(cell_x + 1, cell_y + 1, 0x10000, 0);
            g2c.op(cell_x + 1, cell_y + 1, 0x800, 0);
            return;
        }
    }
    void cottages(int def, bool extra) {                                        // FUN_006804e0
        if ((seed.next() & 3) == 0) { by_road(def, -1); by_road(def, -1); return; }
        by_road(def, -1);
        if (extra && (seed.next() & 1)) by_road(49, -1);
    }
    // ---- act 3's jungle (FUN_0067e910): each 32-tile cell's Def at (col 4,
    // row 4) cells, a clearing moved to its level's set (0x6f2240: +0, +10,
    // +20) with its file from a rolled row of 0x6f2328; the Spider Forest's
    // bottom row the Jungle Head (573), the Flayer Jungle's top row the
    // Tail (574), file 1 when the cell beside is empty.
    [[nodiscard]] bool is_jungle() const { return level.rect.level >= level_ids::kSpiderForest && level.rect.level <= level_ids::kFlayerJungle; }
    void lay_jungle() {
        static constexpr std::array<std::array<int, 3>, 6> kFiles = { { { 0, 1, 2 }, { 1, 0, 2 }, { 0, 2, 1 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } } };
        const int id = level.rect.level, cols = level.rect.width / 32, rows = level.rect.height / 32;
        if (level.jungle.size() < std::size_t(cols * rows)) { note("drlg: jungle level " + std::to_string(id) + " without its cells"); return; }
        const int order = seed(level.jungle_specials == 3 ? 6 : 2);
        std::size_t next = 0;
        int special = 0;
        for (int row = 0; row < rows; ++row) {
            if (id == level_ids::kSpiderForest && row == rows - 1) { place(0, row * 4, 0x23d, level.jungle[std::size_t(rows * cols - 1)] == 0, false); next += 2; continue; }
            if (id == level_ids::kFlayerJungle && row == 0) { place(0, 0, 0x23e, level.jungle[1] == 0, false); next += 2; continue; }
            for (int col = 0; col < cols; ++col) {
                int def = level.jungle[next++], file = -1;
                if (def > 0x23e && special < 3) { def += (id - level_ids::kSpiderForest) * 10; file = kFiles[std::size_t(order)][std::size_t(special++)]; }
                if (def) place(col * 4, row * 4, def, file, false);
            }
        }
    }

    // ---- Kurast (FUN_0067f190, 79-82) and Travincal (FUN_0067f3b0, 83).
    // A row of Defs scattered (FUN_0067eed0): every cell shuffled, each
    // rolls a Def in [low, high] and takes it where it fits, up to `most`
    // (0 = no limit).
    void scatter(int low, int high, int most) {
        int placed = 0;
        for (auto [cell_x, cell_y] : shuffled(cells_wide, cells_high)) {
            const int def = low + seed(high - low + 1);
            if (!fits(cell_x, cell_y, def, 0, 0xf)) continue;
            place(cell_x, cell_y, def, -1, false);
            if (most > 0 && ++placed >= most) return;
        }
    }
    // The walls (FUN_0067ead0 / 0067ec30 / 0067ed70): top and bottom rows
    // (a gap Def + 8 at `top_gap` / `bottom_gap`; a middle gap is two cells
    // wide), the sides, then the corners. Base: top, bottom, right, left,
    // corners top-left, top-right, bottom-left, bottom-right. The Bazaar
    // lays its top and bottom rows in one loop (`together`), which orders
    // the rotations' first rolls differently.
    void kurast_walls(int base, int top_gap, int bottom_gap, bool wide_top, bool wide_bottom, bool together = false) {
        const int right = cells_wide - 1, bottom = cells_high - 1;
        auto top = [&](int x) { place(x, 0, base + (x == top_gap ? 8 : 0), -1, false); };
        auto low = [&](int x) { place(x, bottom, base + 1 + (x == bottom_gap ? 8 : 0), -1, false); };
        if (together)
            for (int x = 1; x < right; ++x) { top(x); low(x); }
        else {
            for (int x = 1; x < right; x += 1 + (wide_top && x == top_gap)) top(x);
            for (int x = 1; x < right; x += 1 + (wide_bottom && x == bottom_gap)) low(x);
        }
        for (int y = 1; y < bottom; ++y) {
            place(right, y, base + 2, -1, false);
            place(0, y, base + 3, -1, false);
        }
        place(0, 0, base + 5, -1, false);
        place(right, 0, base + 4, -1, false);
        place(0, bottom, base + 7, -1, false);
        place(right, bottom, base + 6, -1, false);
    }
    void lay_kurast() {
        const int half = (cells_wide - 1) / 2, first = 1, last = cells_wide - 2, right = cells_wide - 4, bottom = cells_high - 4;
        const bool roll = level.act_roll != 0;
        switch (level.rect.level) {
        case level_ids::kLowerKurast:
            kurast_walls(0x25d, roll ? first : last, half, false, true);
            anywhere(0x277, 0, 0, 0xf);
            scatter(0x26a, 0x26a, 4);
            scatter(0x268, 0x269, 0);
            scatter(0x267, 0x267, 0);
            break;
        case level_ids::kKurastBazaar:
            kurast_walls(0x26b, roll ? last : first, roll ? first : last, false, false, true);
            place(3, 3, 0x275, 0, false);
            place(right, 3, 0x275, 1, false);
            anywhere(0x276, 0, 0, 0xf);
            anywhere(0x276, 1, 0, 0xf);
            anywhere(0x277, 0, 0, 0xf);
            scatter(0x27b, 0x27b, 4);
            scatter(0x279, 0x27a, 0);
            scatter(0x278, 0x278, 0);
            break;
        case level_ids::kUpperKurast:
            kurast_walls(0x27c, half, roll ? last : first, true, false);
            place(3, bottom, 0x286, 0, false);
            place(right, bottom, 0x286, 1, false);
            anywhere(0x287, 0, 0, 0xf);
            anywhere(0x287, 1, 0, 0xf);
            anywhere(0x277, 0, 0, 0xf);
            scatter(0x28b, 0x28b, 4);
            scatter(0x289, 0x28a, 0);
            scatter(0x288, 0x288, 0);
            break;
        case level_ids::kKurastCauseway: place(0, 0, 0x28c, 0, false); break;
        case level_ids::kTravincal:
            for (int k = 0; k < 6; ++k) place(std::array{ 0, 2, 6 }[std::size_t(k % 3)], k / 3 * 4, 0x28d + k, -1, false);
            break;
        default: break;
        }
    }

    // ---- act 2's desert (FUN_0067f980): LevelType 16, levels 41-46.
    [[nodiscard]] bool is_desert() const {                      // FUN_00675360: the Forgotten Sands too
        return (level.rect.level >= level_ids::kRockyWaste && level.rect.level <= level_ids::kCanyonOfTheMagi) || level.rect.level == level_ids::kForgottenSands;
    }
    [[nodiscard]] bool is_mesa() const { return level.rect.level >= level_ids::kOuterSteppes && level.rect.level <= level_ids::kCityOfTheDamned; }
    [[nodiscard]] bool is_chaos() const { return level.rect.level == level_ids::kChaosSanctuary; }
    [[nodiscard]] int border_base() const { return is_desert() ? 0x16c : is_mesa() ? 799 : 4; }   // the border defs FUN_006752a0 is handed
    // FUN_0067f470: from a rolled start, each def in turn anywhere; with
    // `every_file` each of its files.
    void desert_list(std::initializer_list<int> defs, bool every_file) {
        const std::vector<int> list(defs);
        const int count = int(list.size());
        int slot = seed(count);
        for (int k = 0; k < count; ++k, slot = (slot + 1) % count) {
            const int def = list[std::size_t(slot)];
            if (!every_file) { anywhere(def, -1, 0, 0xf); continue; }
            const auto* found = preset(def);
            for (int file = 0; found && file < found->files; ++file) anywhere(def, file, 0, 0xf);
        }
    }
    void lay_desert() {
        const int id = level.rect.level;
        auto subs = [&] { border_subs(2); border_subs(1); border_subs(3); };            // FUN_0067f630
        auto one = [&] {                                                                  // FUN_0067f670
            anywhere(id == 43 ? 0x186 : id == 44 ? 0x19c : id == 45 ? 0x185 : 0x184, -1, 0, 0xf);
        };
        auto five = [&] {                                                                 // FUN_0067f5c0: a row of 0x6f2390
            const auto& row = kDesertRows[std::size_t(seed.next() & 7)];
            for (const auto& [def, file, x, y] : row) place(x, y, def, file, false);
        };
        switch (id) {
        case 41: {                                                                        // FUN_0067f560: by Lut Gholein
            for (const auto& node : nodes)
                if (node.rect.level == level_ids::kLutGholein) {
                    if (node.side == 3) place(0, cells_high - 1, 0x16b, -1, false);
                    else place(cells_wide - 1, 0, 0x16a, -1, false);
                    break;
                }
            subs(); one(); shrines(5);
            desert_list({ 0x18b, 0x19b, 0x191, 0x192, 399, 0x18e, 0x193 }, false);
            break;
        }
        case 42:
            five(); subs(); one(); waypoint(); shrines(5);
            desert_list({ 0x18b, 0x19b, 400, 0x18e }, false);
            desert_list({ 0x194, 0x195, 0x196, 0x197 }, true);
            break;
        case 43:
            five(); subs(); one(); desert_list({ 0x18c, 0x18d }, false); waypoint(); shrines(5);
            desert_list({ 0x19b, 399, 0x18e, 0x193 }, false);
            desert_list({ 0x18b }, true); desert_list({ 0x18b }, true);
            break;
        case 44:
            five(); subs(); one(); desert_list({ 0x19d, 0x198, 0x199, 0x19a }, false); waypoint(); shrines(5);
            desert_list({ 0x18b, 400, 0x18e, 0x194, 0x195 }, false);
            desert_list({ 0x19b }, true); desert_list({ 0x19b }, true);
            break;
        case 45: one(); break;
        case level_ids::kForgottenSands:                                                  // the canyon's gate, then its lists before the shrines
            place(4, 4, 0x18a, -1, false);
            subs();
            desert_list({ 0x191, 0x192, 0x196, 0x197, 0x193 }, false);
            desert_list({ 0x188, 0x189 }, true);
            shrines(5);
            break;
        case 46:                                                                          // FUN_0067f8d0: the canyon's rows (0x6f2610), the tombs' gate
            for (const auto& [def, file, x, y] : kCanyonRows) place(x, y, def, file, false);
            place(4, 4, 0x18a, -1, false);
            subs(); shrines(5);
            desert_list({ 0x191, 0x192, 0x196, 0x197, 0x193 }, false);
            desert_list({ 0x188, 0x189 }, true);
            break;
        default: break;
        }
    }
    // ---- act 4's Mesa (FUN_0067e890, LevelType 27): levels 104-106.
    void lay_mesa() {
        const int lid = level.rect.level;
        // 1. FUN_0067e660: 104's flags 0x400000 -> FortMesa (798, 8x24) at cell (0, 1); 0x800000 -> at (0, 4)
        if (lid == level_ids::kOuterSteppes) {
            if (level.rect.flags & 0x400000) place(0, 1, 798, -1, false);
            else if (level.rect.flags & 0x800000) place(0, 4, 798, -1, false);
        }
        // 2. Border LvlSub FUN_006752a0 types 1, 2, 3, base 799
        border_subs(1);
        border_subs(2);
        border_subs(3);
        // 3. FUN_0067e6a0: anywhere(def, -1, 0, 0xf) in order
        const int m_base = lid == level_ids::kOuterSteppes ? 812 : lid == level_ids::kPlainsOfDespair ? 817 : 823;
        const int p_base = lid == level_ids::kOuterSteppes ? 828 : 832;
        if (lid == level_ids::kCityOfTheDamned) anywhere(811, -1, 0, 0xf); // 0x32b, WarpLava
        anywhere(m_base, -1, 0, 0xf);
        anywhere(m_base + 1, -1, 0, 0xf);
        anywhere(m_base + 1, -1, 0, 0xf);
        anywhere(m_base + 2, -1, 0, 0xf);
        anywhere(m_base + 2, -1, 0, 0xf);
        anywhere(m_base + 3, -1, 0, 0xf);
        anywhere(m_base + 3, -1, 0, 0xf);
        if (lid == level_ids::kPlainsOfDespair) anywhere(822, -1, 0, 0xf); // 0x336, Izual's 8x8
        for (int i = 0; i < 4; ++i) anywhere(m_base + 4, -1, 0, 0xf);
        anywhere(p_base, -1, 0, 0xf);
        anywhere(p_base + 1, -1, 0, 0xf);
        anywhere(p_base + 1, -1, 0, 0xf);
        anywhere(p_base + 2, -1, 0, 0xf);
        anywhere(p_base + 2, -1, 0, 0xf);
        for (int i = 0; i < 4; ++i) anywhere(p_base + 3, -1, 0, 0xf);
    }
    // ---- act 4's Chaos Sanctuary (FUN_0067e840): LevelType 27, level 108.
    // ---- act 5's barricades (FUN_0067e600, LevelType 31: 111, 112, 117).
    // Every piece is 16x16 (2x2 cells); the Frozen Tundra has its own set
    // (957.. for 881..).
    [[nodiscard]] bool is_barricade() const {
        const int id = level.rect.level;
        return id == level_ids::kFrigidHighlands || id == level_ids::kArreatPlateau || id == level_ids::kFrozenTundra;
    }
    [[nodiscard]] bool tundra() const { return level.rect.level == level_ids::kFrozenTundra; }
    // FUN_0067dcf0: the outline's edges every 2 cells (881 + kind), its
    // corners (880 + row, FUN_00675600), a contact span's two openings
    // flagged 0x400 halfway along.
    void barricade_borders() {
        const int base = tundra() ? 957 : 881;
        int vert_index = head;
        do {
            const auto& vert = verts[std::size_t(vert_index)];
            const auto& next = verts[std::size_t(vert.next)];
            const auto& after = verts[std::size_t(next.next)];
            const int dx = sgn(next.x - vert.x), dy = sgn(next.y - vert.y), dx2 = sgn(after.x - next.x), dy2 = sgn(after.y - next.y);
            const int kind = dx == -1 ? 0 : dy == -1 ? 1 : dx == 1 ? 2 : 3;
            int x = vert.x & ~1, y = vert.y & ~1;
            const int next_x = next.x & ~1, next_y = next.y & ~1;
            if (!(vert.flags & 2))
                for (int steps = 0; (x != next_x || y != next_y) && steps < cells_wide + cells_high; ++steps) {
                    x += 2 * dx;
                    y += 2 * dy;
                    place(x, y, base + kind, -1, false);
                    g2c.op(x, y, 1, 0);
                }
            if (vert.flags & 1) {
                const int open_x = (std::max(vert.x, next.x) - 4 * std::abs(dx)) & ~1, open_y = (std::max(vert.y, next.y) - 4 * std::abs(dy)) & ~1;
                g2c.op(open_x, open_y, 0x400, 0);
                g2c.op(open_x + 2 * std::abs(dx), open_y + 2 * std::abs(dy), 0x400, 0);
            }
            if (const int row = corner_row(sx2(2 * dx) + 9 * (sx2(2 * dx2) + 2 * dy2) + 2 * dy)) {
                place(next_x, next_y, base + row - 1, -1, false);
                g2c.op(next_x, next_y, 1, 0);
            }
            vert_index = vert.next;
        } while (vert_index != head);
        if (level.rect.level == level_ids::kFrigidHighlands) {
            g2c.op(cells_wide - 2, cells_high - 4, 0x400, 0);
            g2c.op(cells_wide - 2, cells_high - 3, 0x400, 0);
        }
    }
    // FUN_0067def0: from the top right round to the bottom left, each piece
    // its inner twin (+12), the way on by its kind (0x6f1fd8); the two ends.
    void barricade_inside() {
        static constexpr std::array<std::pair<int, int>, 12> kWalk = { { { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 0 },
                                                                        { 0, 1 }, { -1, 0 }, { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 } } };
        const int base = tundra() ? 957 : 881;
        int x = cells_wide - 2, y = 0;
        for (int steps = 0; (x != 0 || y != cells_high - 2) && steps < 2 * (cells_wide + cells_high); ++steps) {
            const int kind = int(g04.get(x, y)) - base;
            if (kind < 0 || kind >= 12) { note("drlg: barricade walk off its border"); break; }   // game.exe reads past its table
            place(x, y, kind + base + 12, -1, false);
            x += 2 * kWalk[std::size_t(kind)].first;
            y += 2 * kWalk[std::size_t(kind)].second;
        }
        place(cells_wide - 2, 0, base + 25, -1, false);
        place(0, cells_high - 2, base + 24, -1, false);
    }
    // FUN_0067db50: the first opening on each edge (top, bottom, left,
    // right) takes its way through (909, 908, 910, 907).
    void barricade_exits() {
        const int file = -1;
        for (int x = 0; x < cells_wide; ++x) if (g2c.get(x, 0) & 0x400) { place(x, 0, 0x38d, file, false); break; }
        for (int x = 0; x < cells_wide; ++x) if (g2c.get(x, cells_high - 2) & 0x400) { place(x, cells_high - 2, 0x38c, file, false); break; }
        for (int y = 0; y < cells_high; ++y) if (g2c.get(0, y) & 0x400) { place(0, y, 0x38e, file, false); break; }
        for (int y = 0; y < cells_high; ++y) if (g2c.get(cells_wide - 2, y) & 0x400) { place(cells_wide - 2, y, 0x38b, file, false); break; }
    }
    // FUN_0067da70 (0x6f1f9c): the Arreat Plateau's and the Frozen Tundra's
    // way on, by the level's shape {level, file, far side, tall def, wide def}.
    void barricade_ways() {
        struct Way { int level, file, far_side, tall, wide; };
        static constexpr std::array<Way, 3> kWays = { { { 112, 0, 0, 913, 914 }, { 117, 0, 1, 983, 984 }, { 117, 0, 0, 985, 986 } } };
        for (const auto& way : kWays) {
            if (way.level != level.rect.level) continue;
            if (level.rect.width > level.rect.height) place(way.far_side ? cells_wide - 2 : 0, 2, way.wide, way.file, false);
            else place(2, way.far_side ? cells_high - 2 : 0, way.tall, way.file, false);
        }
    }
    // FUN_0067e4b0: the Frigid Highlands' corner piece (880) and 896 above it.
    void highlands_corner() {
        const auto [corner_w, corner_h] = cells_of(0x370);
        const int x = cells_wide - corner_w, y = cells_high - corner_h;
        place(x, y, 0x370, -1, false);
        place(x, y - 2, 0x380, -1, false);
    }
    // FUN_0067e240: three of the Frigid Highlands' barricade pieces
    // (915..922) become their doors (+16): 90 random tries, then a sweep.
    void highlands_doors() {
        int count = 0;
        auto try_at = [&](int x, int y) {
            const int def = (g2c.get(x, y) & 0x200) ? int(g04.get(x, y)) : 0;   // FUN_00674120
            if (def < 915 || def > 922) return;
            place(x, y, def + 16, -1, false);
            ++count;
        };
        for (int tries = 0; tries < 90 && count < 3; ++tries) {
            const int x = seed(cells_wide / 2) * 2;
            const int y = seed(cells_high / 2) * 2;
            try_at(x, y);
        }
        const int start_x = seed(cells_wide / 2), start_y = seed(cells_high / 2);
        for (int y = 0; y < cells_high && count < 3; ++y)
            for (int x = 0; x < cells_wide && count < 3; ++x) try_at((x + start_x * 2) % cells_wide, (y + start_y * 2) % cells_high);
        if (count < 3) note("drlg: the Frigid Highlands' doors don't fit");   // game.exe asserts (OutSiege.cpp 0x259)
    }
    // FUN_0067e160 (0x6f2100): each row's def (by the level's shape) anywhere,
    // `count` times; {level, tall def, wide def, file, count}.
    void barricade_lists() {
        struct Item { int level, tall, wide, file, count; };
        static constexpr std::array<Item, 15> kItems = { { { 111, 955, 956, 0, 1 }, { 112, 955, 956, 0, 1 }, { 117, 955, 956, 1, 1 }, { 112, 953, 953, -1, 1 },
                                                          { 117, 954, 954, -1, 1 }, { 111, 944, 947, -1, 1 }, { 111, 942, 945, -1, 4 }, { 111, 943, 946, -1, 4 },
                                                          { 112, 941, 941, -1, 1 }, { 112, 939, 939, -1, 1 }, { 112, 940, 940, -1, 5 }, { 117, 948, 948, -1, 4 },
                                                          { 117, 949, 949, -1, 4 }, { 117, 950, 950, -1, 4 }, { 117, 951, 951, -1, 3 } } };
        const bool tall = level.rect.width < level.rect.height;
        for (const auto& item : kItems)
            if (item.level == level.rect.level)
                for (int k = 0; k < item.count; ++k) anywhere(tall ? item.tall : item.wide, item.file, 0, 0xf);
    }
    void lay_barricade() {
        barricade_borders();
        barricade_inside();
        barricade_exits();
        barricade_ways();
        if (level.rect.level == level_ids::kFrigidHighlands) highlands_corner();
        border_subs(12);
        if (level.rect.level == level_ids::kFrigidHighlands) highlands_doors();
        barricade_lists();
    }

    // ---- act 5's Bloody Foothills (FUN_0067e560, level 110): the siege's
    // 15 strips (LvlPrest 865..879), right to left from the east edge.
    void lay_siege() {
        const auto [strip_w, strip_h] = cells_of(0x361);
        if (strip_w <= 0) { note("drlg: siege strip 865 missing"); return; }
        for (int i = 0, x = cells_wide - strip_w; i < 15; ++i, x -= strip_w) {
            if (x < 0) { note("drlg: the siege strips don't fit"); return; }   // game.exe asserts (OutSiege.cpp 0x294)
            place(x, 0, 0x361 + i, 0, false);
        }
    }
    void lay_chaos() {
        for (int i = 0; i < 25; ++i)
            place((i % 5) * 3, (i / 5) * 3, kChaosPresets[std::size_t(i)], -1, false);
    }
    void fills() {
        auto any = [&](int def) { anywhere(def, -1, 0, 0xf); };
        switch (level.rect.level) {
        case 2: by_road(46, -1); cottages(47, false); break;                   // pond
        case 3: cottages(48, true); any(44); break;
        case 4: by_road(160, -1); by_road(45, -1); any(162); cottages(47, true); cottages(42, false); any(31); return;
        case 5: any(161); any(41); any(40); cottages(48, true); cottages(43, false); break;
        case 6: any(163); any(38); any(39); cottages(47, true); cottages(42, false); break;
        case 7: cottages(48, true); cottages(43, false); any(31); return;
        case 17: place(1, 1, 108, -1, false); return;                           // the Mausoleum
        case 39: any(50); any(46); any(31); any(38); any(39); break;
        default: note("drlg: fills for level " + std::to_string(level.rect.level) + " not implemented"); return;
        }
        anywhere(29, -1, 0, 0xf);
        anywhere(30, -1, 0, 0xf);
    }
};

// ---- a plain room's tiles (FUN_0067d2d0)
using Room = PlainRoom;

inline void stamp_room(Room& room, const OutdoorData& data, const OutdoorLevel& level, d2d::rules::Rng& rng,
                       int type, int theme, std::uint32_t mask, std::vector<std::string>& notes) {
    if (type < 0 || theme < 0 || theme > 4) return;
    std::size_t first = 0;
    while (first < data.subs.size() && data.subs[first].type != type) ++first;
    for (std::size_t i = 0; mask; ++i, mask >>= 1) {
        if (!(mask & 1) || first + i >= data.subs.size() || data.subs[first + i].type != type) continue;
        const Sub& sub = data.subs[first + i];
        if (!sub.map) continue;
        if (sub.check_all) {
            const std::string message = "drlg: LvlSub CheckAll stamps not implemented";
            if (std::ranges::find(notes, message) == notes.end()) notes.push_back(message);
            continue;
        }
        const auto& map = *sub.map;
        const auto& groups = map.groups();
        auto src = [&](const std::vector<d2d::ds1::Layer>& layers, int x, int y) { return Gen::word(map, layers, x, y); };
        auto orient0 = [&](int x, int y) -> std::uint32_t {
            if (map.walls().empty() || x < 0 || y < 0 || x >= map.width() || y >= map.height()) return 0;
            const auto& tile = map.walls()[0].cells[std::size_t(y) * std::size_t(map.width()) + std::size_t(x)];
            return std::uint32_t(tile.wall_type) | tile.wall_zero << 8;
        };
        auto fit = [&](const d2d::ds1::Group& group, int x, int y) {                // FUN_0066fcf0
            for (int group_y = 0; group_y < group.height; ++group_y)
                for (int group_x = 0; group_x < group.width; ++group_x) {
                    const auto floor_word = src(map.floors(), group.x + group_x, group.y + group_y), wall_word = src(map.walls(), group.x + group_x, group.y + group_y);
                    if (!(floor_word & 2) && !(!map.walls().empty() && (wall_word & 1))) continue;
                    const auto index = std::size_t((y + group_y) * 9 + x + group_x);
                    if ((room.floor[index] & 0x3f0ff00) || !(room.floor[index] & 2) || (room.wall[index] & 1)) return false;
                }
            return true;
        };
        auto stamp = [&](const d2d::ds1::Group& group, int x, int y) {              // FUN_0066fad0
            for (int group_y = 0; group_y < group.height; ++group_y)
                for (int group_x = 0; group_x < group.width; ++group_x) {
                    const auto index = std::size_t((y + group_y) * 9 + x + group_x);
                    const auto floor_word = src(map.floors(), group.x + group_x, group.y + group_y);
                    if (floor_word & 2) room.floor[index] = floor_word | 0x80;
                    const auto wall_word = src(map.walls(), group.x + group_x, group.y + group_y);
                    if (wall_word & 1) room.wall[index] = wall_word;
                    if (const auto orient = orient0(group.x + group_x, group.y + group_y)) room.orient[index] = orient;
                    const auto shadow_word = src(map.shadows(), group.x + group_x, group.y + group_y);
                    if (!(shadow_word & 0x8000000)) continue;
                    if (data.dt1s) {                                                // FUN_0066e060: picked now, on the room seed
                        const auto [tile_file, tile_index] = pick_tile(room_dt1_list(room.dt1_mask, *data.dt1s), rng, 13, shadow_word);
                        room.tiles.push_back({ 2, x + group_x, y + group_y, 13, tile_file, tile_index, shadow_word });   // FUN_0066df40 flags it on its word
                    } else {
                        room.shadow[index] = shadow_word;
                    }
                }
            for (const auto& unit : ds1_units(map, data.ids)) {                          // FUN_0066fa10: the group's objects
                const int unit_x = group.x * 5, unit_y = group.y * 5;
                if (unit_x < unit.x && unit_y < unit.y && unit.x < unit_x + group.width * 5 && unit.y < unit_y + group.height * 5)
                    room.units.insert(room.units.begin(), { unit.type, unit.id, unit.mode, unit.x - unit_x + x * 5, unit.y - unit_y + y * 5, unit.flags });
            }
        };
        const int max = sub.max[std::size_t(theme)];
        if (groups.empty() || max < 1) continue;
        for (int attempt = 0; attempt < max; ++attempt) {                                          // FUN_00670170
            const auto& group = groups[std::size_t(rng(int(groups.size())))];
            const int spots_x = 8 - group.width, spots_y = 8 - group.height;
            if (spots_x < 1 || spots_y < 1) continue;
            const int trials = sub.trials[std::size_t(theme)];
            if (trials == -1) {
                const int spots = spots_x * spots_y;
                std::vector<std::pair<int, int>> spots_list;
                for (int k = 0; k < spots; ++k) spots_list.emplace_back(k % spots_x, k / spots_x);
                for (int k = 0; k < spots; ++k) {
                    const int first_pick = rng(spots), second_pick = rng(spots);
                    std::swap(spots_list[std::size_t(first_pick)], spots_list[std::size_t(second_pick)]);
                }
                for (auto [spot_x, spot_y] : spots_list)
                    if (fit(group, spot_x + 1, spot_y + 1)) { stamp(group, spot_x + 1, spot_y + 1); break; }
            } else {
                for (int k = 0; k < trials; ++k) {
                    const int x = rng(spots_x) + 1, y = rng(spots_y) + 1;
                    if (fit(group, x, y)) { stamp(group, x, y); break; }
                }
            }
        }
    }
    (void)level;
}

inline void room_tiles(Room& room, const OutdoorData& data, const OutdoorLevel& level,
                       const std::vector<std::vector<std::pair<int, int>>>& roads, std::vector<std::string>& notes) {
    d2d::rules::Rng rng{ room.seed_low };                    // FUN_0066ee40
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) room.floor[std::size_t(y * 9 + x)] = 0x40002;
    // Roads (FUN_00680c80, act 1 only): a mask over the room grown by one.
    const bool desert = level.rect.level >= level_ids::kRockyWaste && level.rect.level <= level_ids::kCanyonOfTheMagi;
    std::array<std::uint8_t, 121> mask{};
    const int origin_x = level.rect.x + room.x - 1, origin_y = level.rect.y + room.y - 1;
    auto dot = [&](int x, int y) {
        x -= origin_x; y -= origin_y;
        if (x >= 0 && y >= 0 && x < 11 && y < 11) mask[std::size_t(y * 11 + x)] = 1;
    };
    for (const auto& line : desert ? std::vector<std::vector<std::pair<int, int>>>{} : roads)
        for (std::size_t i = 0; i + 1 < line.size(); ++i) {                     // FUN_0067c8e0
            int x = line[i].first, y = line[i].second;
            const int dx = std::abs(line[i + 1].first - x), dy = std::abs(line[i + 1].second - y);
            const int step_x = line[i + 1].first < x ? -1 : 1, step_y = line[i + 1].second < y ? -1 : 1;
            int err = 0;
            if (dx < dy) {
                for (int k = 0; k < 2; ++k) dot(x + k, y);
                for (int step = 0; step < dy; ++step) {
                    y += step_y;
                    err += dx;
                    if (err > dy) { x += step_x; err -= dy; }
                    for (int k = 0; k < 2; ++k) dot(x + k, y);
                }
            } else {
                for (int k = 0; k < 2; ++k) dot(x, y + k);
                for (int step = 0; step < dx; ++step) {
                    x += step_x;
                    err += dy;
                    if (err > dx) { y += step_y; err -= dx; }
                    for (int k = 0; k < 2; ++k) dot(x, y + k);
                }
            }
        }
    auto masked = [&](int x, int y) { return int(mask[std::size_t(y * 11 + x)] != 0); };
    for (int x = 1; x <= 9; ++x)
        for (int y = 9; y >= 1; --y) {
            if (!masked(x, y)) continue;
            const int idx = masked(x - 1, y + 1) | masked(x - 1, y) << 1 | masked(x - 1, y - 1) << 2 | masked(x, y + 1) << 3
                          | masked(x, y - 1) << 4 | masked(x + 1, y + 1) << 5 | masked(x + 1, y) << 6 | masked(x + 1, y - 1) << 7;
            if (idx && kRoad[std::size_t(idx)])
                room.floor[std::size_t((y - 1) * 9 + x - 1)] = std::uint32_t(kRoad[std::size_t(idx)]) << 8 | 0x82;
        }
    stamp_room(room, data, level, rng, level.sub_waypoint, 0, (room.flags >> 16) & 3, notes);
    stamp_room(room, data, level, rng, level.sub_shrine, 0, (room.flags >> 12) & 0xf, notes);
    stamp_room(room, data, level, rng, level.sub_type, level.sub_theme, room.theme_mask, notes);
    // The LevelType's ground (FUN_0067d2d0), ORed into every floor cell
    // nothing has claimed (no bits of 0x3f0ff80): the desert 0x100, the
    // jungle 0x120000, Kurast 0x100000, Act 5's 0xa00000 / 0x1600000, the
    // Frozen Tundra (117) 0x600000.
    const std::uint32_t ground = level.level_type == 0x10 ? 0x100u : level.level_type == 0x15 ? 0x120000u : level.level_type == 0x16 ? 0x100000u
                               : level.level_type == 0x1b ? 0xa00000u : level.level_type == 0x1c ? 0x1600000u
                               : level.level_type == 0x1f && level.rect.level == level_ids::kFrozenTundra ? 0x600000u : 0u;
    if (ground)
        for (auto& floor_word : room.floor)
            if ((floor_word & 0x3f0ff80) == 0) floor_word |= ground;
    room.seed = rng;
    // FUN_0067c600 on the wall and floor grids: every edge cell | 4 (its tile
    // may be shared with the room next to it, FUN_0066e940).
    for (int i = 0; i < 9; ++i)
        for (const int edge : { i, 72 + i, i * 9, i * 9 + 8 }) { room.wall[std::size_t(edge)] |= 4; room.floor[std::size_t(edge)] |= 4; }
}

}  // namespace outdoor_detail

// The act 1 wilderness level `L` from its seed. The tiles come out
// level-relative: 4 wall layers, 2 floors, 1 shadow.
// ponytail: plain-room tiles take a DT1 variant at draw time (first
// match), not game.exe's rarity pick on the room seed (FUN_0066d820).
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

inline Outdoor generate_outdoor(const OutdoorData& data, const OutdoorLevel& level, d2d::rules::Rng seed) {
    using namespace outdoor_detail;
    Outdoor out;
    Gen gen(data, level, seed, out.notes);
    const int id = level.rect.level;                    // FUN_006807f0
    const bool wild = id >= level_ids::kBloodMoor && id <= level_ids::kTamoeHighland;
    const bool desert = gen.is_desert(), act3 = id >= level_ids::kSpiderForest && id <= level_ids::kTravincal;
    const bool act4 = gen.is_mesa() || gen.is_chaos();
    const bool siege = id == level_ids::kBloodyFoothills, barricade = gen.is_barricade();
    if (!wild && !desert && !act3 && !act4 && !siege && !barricade && id != level_ids::kBurialGrounds && id != level_ids::kMooMooFarm) gen.note("drlg: outdoor level " + std::to_string(id) + " not implemented");
    gen.neighbours();
    gen.outline();
    if (siege) gen.lay_siege();                                                           // FUN_0067e600: nothing else
    else if (barricade) {
        gen.contacts();
        gen.lay_barricade();
    } else if (act3) {                                                                           // FUN_0067f450: no borders
        gen.contacts();
        if (gen.is_jungle()) gen.lay_jungle();
        gen.lay_kurast();
    } else if (act4) {                                                                    // FUN_0067e890: the sanctuary has no borders
        gen.contacts();
        if (gen.is_chaos()) gen.lay_chaos();
        else {
            gen.borders();
            gen.lay_mesa();
        }
    } else if (desert) {                                                                  // FUN_0067f980
        gen.contacts();
        gen.borders();
        gen.lay_desert();
    } else {
    if (id != level_ids::kBloodMoor && id != level_ids::kColdPlains && id != level_ids::kBurialGrounds) gen.cliffs();
    gen.contacts();
    gen.borders();
    if (wild) {
        gen.border_subs(0);
        gen.features();
        gen.border_subs(1);
        gen.border_subs(2);
        gen.transitions();
        gen.border_subs(3);
        out.roads = gen.roads();
    }
    if (id == level_ids::kMooMooFarm)
        for (int sub = 0; sub < 4; ++sub) gen.border_subs(sub);
    if (id >= level_ids::kColdPlains && id <= level_ids::kBlackMarsh) gen.waypoint();
    if (wild) gen.shrines(5);
    gen.fills();
    }

    // The finish (FUN_006750f0): rooms in cell order, each allocation
    // stepping the level seed.
    const int width = level.rect.width, height = level.rect.height;
    out.tiles = d2d::ds1::Map(width, height, 4, 2);
    auto alloc = [&] {                                  // FUN_0066b3e0
        gen.seed.next();
        d2d::rules::Rng rng{ gen.seed.low };
        rng.next();
        return rng;
    };
    auto put = [&](int layer_kind, int layer, int x, int y, std::uint32_t word, std::uint32_t orient) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        auto& layers = layer_kind == 0 ? out.tiles.floors() : layer_kind == 1 ? out.tiles.walls() : out.tiles.shadows();
        if (std::size_t(layer) >= layers.size()) return;
        auto tile = d2d::ds1::Map::tile(word);
        tile.wall_type = std::uint8_t(orient & 0xff);
        tile.wall_zero = orient >> 8;
        layers[std::size_t(layer)].cells[std::size_t(y) * std::size_t(width) + std::size_t(x)] = tile;
    };
    std::vector<Room> rooms;
    for (int cell_y = 0; cell_y < gen.cells_high; ++cell_y)
        for (int cell_x = 0; cell_x < gen.cells_wide; ++cell_x) {
            const auto flags = gen.g2c.get(cell_x, cell_y);
            if (flags & 0x200) {
                const int def = int(gen.g04.get(cell_x, cell_y));
                if (!def) continue;
                const auto* preset = gen.preset(def);
                if (!preset) { gen.note("drlg: LvlPrest def " + std::to_string(def) + " missing"); continue; }
                (void)gen.seed(preset->files);                   // FUN_00666ed0: rolled, then replaced
                const int file = int((flags >> 16) & 0xf);
                const auto* map = file < 6 ? preset->maps[std::size_t(file)] : nullptr;
                // FUN_00667970: with Scan or Pops the units roll to stay now, on the level seed.
                const bool rolled = map && (preset->scan || preset->pops);
                std::vector<Unit> units;
                if (rolled) {
                    units = ds1_units(*map, data.ids);
                    std::erase_if(units, [&](const Unit& unit) { return !stays(unit, data.ids, gen.seed); });
                }
                for (int tile_y = 0; tile_y < preset->height; tile_y += 8)
                    for (int tile_x = 0; tile_x < preset->width; tile_x += 8)
                    {
                        const auto room_seed = alloc();
                        out.rooms.push_back({ cell_x * 8 + tile_x, cell_y * 8 + tile_y, room_seed.low, 8, 8, 2, def, file, cell_x * 8, cell_y * 8, rolled, units, room_seed.high });
                    }
                if (!map) { gen.note("drlg: preset " + std::to_string(def) + " file " + std::to_string(file) + " not loaded"); continue; }
                const int origin_x = cell_x * 8, origin_y = cell_y * 8;
                for (int y = 0; y < preset->height && y < map->height() && origin_y + y < height; ++y)
                    for (int x = 0; x < preset->width && x < map->width() && origin_x + x < width; ++x) {
                        const auto index = std::size_t(y) * std::size_t(map->width()) + std::size_t(x);
                        for (std::size_t layer_index = 0; layer_index < map->floors().size(); ++layer_index)
                            out.tiles.floors()[std::min<std::size_t>(layer_index, 1)].cells[std::size_t(origin_y + y) * std::size_t(width) + std::size_t(origin_x + x)]
                                = map->floors()[layer_index].cells[index];
                        for (std::size_t layer_index = 0; layer_index < map->walls().size() && layer_index < 4; ++layer_index)
                            out.tiles.walls()[layer_index].cells[std::size_t(origin_y + y) * std::size_t(width) + std::size_t(origin_x + x)] = map->walls()[layer_index].cells[index];
                        if (!map->shadows().empty())
                            out.tiles.shadows()[0].cells[std::size_t(origin_y + y) * std::size_t(width) + std::size_t(origin_x + x)] = map->shadows()[0].cells[index];
                    }
            } else if (!(flags & 0x100)) {                    // FUN_0067d540
                auto room_rng = alloc();
                out.rooms.push_back({ cell_x * 8, cell_y * 8, room_rng.low });
                Room room{ cell_x * 8, cell_y * 8, gen.g18.get(cell_x, cell_y), room_rng.low, 0, plain_dt1_mask(level.level_type), {}, {}, {}, {}, {}, {} };
                if (level.sub_type != -1 && level.sub_theme != -1) {                     // FUN_006706a0
                    std::uint32_t bit = 0;
                    for (const auto& sub : data.subs) {
                        if (sub.type != level.sub_type) { if (bit) break; continue; }
                        if (int(room_rng.next() % 100) < sub.prob[std::size_t(level.sub_theme)]) { room.theme_mask |= 1u << bit; room.dt1_mask |= sub.dt1_mask; }
                        ++bit;
                    }
                }
                rooms.push_back(std::move(room));
            }
        }
    for (auto& room : rooms) {
        room_tiles(room, data, level, out.roads, out.notes);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const auto index = std::size_t(y * 9 + x);
                put(0, 0, room.x + x, room.y + y, room.floor[index], 0);
                if (room.wall[index] & 1) put(1, 0, room.x + x, room.y + y, room.wall[index], room.orient[index]);
                if (room.shadow[index]) put(2, 0, room.x + x, room.y + y, room.shadow[index], 13);
            }
    }
    out.plain = std::move(rooms);
    out.cells_wide = gen.cells_wide;
    out.flags = gen.flags;
    out.cells_high = gen.cells_high;
    out.g04 = gen.g04.values;
    out.g18 = gen.g18.values;
    out.g2c = gen.g2c.values;
    return out;
}

}  // namespace d2d::drlg
