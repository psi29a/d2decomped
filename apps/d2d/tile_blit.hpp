// SPDX-License-Identifier: GPL-3.0-or-later
// DT1 tile blitters: floors, shadows, walls, plain or lit, into the RGBA framebuffer.
#pragma once

#include <dt1.hpp>
#include <light.hpp>
#include <palette.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace d2d::client {

// A see-through hole in what's drawn (the roof cut-out round each player).
struct Hole {
    std::vector<std::pair<int, int>> centres;       // screen centres (a player each: local co-op's several), pixels
    int radius = 0;
    // The alpha a pixel at (px, py) keeps: 0x40 in a hole's inner half,
    // rising to 0xff at its rim.
    [[nodiscard]] int alpha(int pixel_x, int pixel_y, int base_alpha) const {
        int out = base_alpha;
        for (const auto& [centre_x, centre_y] : centres) {
            const int dx = pixel_x - centre_x, dy = pixel_y - centre_y, distance_sq = dx * dx + dy * dy;
            if (distance_sq >= radius * radius) continue;
            const float fraction = std::clamp((std::sqrt(float(distance_sq)) - float(radius) / 2) / (float(radius) / 2), 0.f, 1.f);
            out = std::min(out, 0x40 + int(float(0xff - 0x40) * fraction));
        }
        return out;
    }
};

// Blit one DT1 tile's pre-decoded palette-indexed pixels through `pal`.
// The tile's pixel buffer is (tile.width x abs(tile.height)); index 0 is
// transparent. Screen position is the buffer's top-left; caller does the
// iso math to place it. Clipped to the framebuffer once, by rows and
// columns: off-screen tiles are clipped rather than skipped so the
// compositor can walk the whole grid.
void blit_dt1_tile(std::vector<std::uint8_t>& framebuffer,
                   const d2d::dt1::Tile& tile,
                   const d2d::palette::Palette& pal,
                   int screen_x, int screen_y, int alpha_all = 255, const Hole* hole = nullptr);

// A shadow tile (orientation 13), Blended Shadows on (the Video Options
// default): through the palette's alpha table 0 (PL2 +0x3500) as
// T0[ground][tile], a quarter of the ground under three quarters of the
// tile, unlit (driver +0xa4, callback FUN_004f82d0 at alpha 0xc0). Drawn
// once every floor is down (FUN_004df510).
// ponytail: the mix in RGB, not the table's nearest palette colour.
void blit_dt1_shadow(std::vector<std::uint8_t>& framebuffer, const d2d::dt1::Tile& tile,
                     const d2d::palette::Palette& pal, int screen_x, int screen_y);

// A frame's light (docs/research/re/lighting.md): the grid round the
// player and the palette at each of its 32 levels. game.exe shades a
// tile's 32-pixel blocks between the light at their corners, a subtile's
// (FUN_004de260 → FUN_004f84f0) and draws a unit at the light where it
// stands; here a pixel takes the light bilinear between the subtile
// corners round where it lies on the ground.
struct Lighting {
    d2d::rules::LightGrid grid;
    const std::array<d2d::palette::Palette, 32>* pal = nullptr;
    int ambient = 255;                                     // the day's (or the level's) light, before any lamp
    int environment = 255;                                 // the environment's (+0xc, FUN_0061c0e0): what raised roofs take
    struct Stamp { int x, y, radius8; bool shadowed; };   // eighths of a subtile, as game.exe's light list (0x7b5668)
    std::vector<Stamp> stamps;                             // what lit it (devctl `debug light`)
    // The light at (x, y), cells, 0..255.
    [[nodiscard]] int at(float x, float y) const {
        const float subtile_x = x * 5, subtile_y = y * 5;
        const int grid_x = int(std::floor(subtile_x)), grid_y = int(std::floor(subtile_y));
        const float frac_x = subtile_x - float(grid_x), frac_y = subtile_y - float(grid_y);
        const auto top_left = float(grid.at(grid_x, grid_y)), top_right = float(grid.at(grid_x + 1, grid_y));
        const auto bottom_left = float(grid.at(grid_x, grid_y + 1)), bottom_right = float(grid.at(grid_x + 1, grid_y + 1));
        return int((top_left + (top_right - top_left) * frac_x) * (1 - frac_y) + (bottom_left + (bottom_right - bottom_left) * frac_x) * frac_y);
    }
    [[nodiscard]] const d2d::palette::Palette& palette(float x, float y) const { return (*pal)[std::size_t(at(x, y) >> 3)]; }
    // A unit's light: its own subtile's entry (FUN_00475aa0; the unit's path
    // keeps the byte, FUN_00620100), not a blend. A blend would pull in the
    // dark subtiles of a wall it stands against.
    [[nodiscard]] int unit_at(float x, float y) const { return grid.at(int(std::floor(x * 5)), int(std::floor(y * 5))); }
};

// A floor (or a flat roof) of cell (cell_x, cell_y), lit as game.exe's
// software modes do (FUN_004ddef0's 8 x 8 table round the cell, the block
// callback FUN_004f8b80 per 32 x 16 block; lighting.md). One level for the
// tile when the table shares it.
void blit_dt1_tile_lit(std::vector<std::uint8_t>& framebuffer, const d2d::dt1::Tile& tile, const Lighting& light,
                       int screen_x, int screen_y, int cell_x, int cell_y, int alpha_all = 255, const Hole* hole = nullptr);

// A wall of cell (cell_x, cell_y), lit as game.exe's wall draws do
// (FUN_004df1c0, FUN_004dedf0 for lower walls; the driver's +0x9c): six lights on
// the ground along the cell by the DT1's direction (0x6db9d8 counts,
// 0x72a9e8 spots; 0x72abc8 for a wall in front, tile +0x24 bit 0), one per
// 32-pixel column edge, each column's pixels between its two.
void blit_dt1_wall_lit(std::vector<std::uint8_t>& framebuffer, const d2d::dt1::Tile& tile, const Lighting& light,
                       int screen_x, int screen_y, int cell_x, int cell_y, bool in_front, int alpha_all = 255, const Hole* hole = nullptr);

}  // namespace d2d::client
