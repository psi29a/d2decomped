// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for tile_blit.hpp.
#include "tile_blit.hpp"

#include "common.hpp"

#include <dt1.hpp>
#include <light.hpp>
#include <palette.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace d2d::client {

namespace {

// Draws a tile's buffer at (screen_x, screen_y), clipped to the framebuffer
// once up front: each non-transparent pixel (index 0 is see-through) takes
// colour_at(x, y, index), x and y in the tile's buffer, blended at
// alpha_all (or the hole's alpha there). Opaque draws skip the blend.
template <class ColourAt>
void blit_tile_pixels(std::vector<std::uint8_t>& framebuffer, const d2d::dt1::Tile& tile,
                      int screen_x, int screen_y, int alpha_all, const Hole* hole, ColourAt colour_at) {
    const int screen_w = fb_width(framebuffer);
    const int x_begin = std::max(0, -screen_x), x_end = std::min(tile.width, screen_w - screen_x);
    const int y_begin = std::max(0, -screen_y), y_end = std::min(std::abs(tile.height), int(kScreenHeight) - screen_y);
    if (x_begin >= x_end) return;
    const bool opaque = !hole && alpha_all >= 255;
    for (int y = y_begin; y < y_end; ++y) {
        const int pixel_y = screen_y + y;
        const auto* row = tile.pixels.data() + std::size_t(y) * std::size_t(tile.width);
        auto* line = framebuffer.data() + std::size_t(pixel_y) * std::size_t(screen_w) * 4;   // the row's first pixel; screen_x + x >= 0 inside the clip
        if (opaque) {
            for (int x = x_begin; x < x_end; ++x) {
                const std::uint8_t idx = row[x];
                if (idx == 0) continue;
                const auto colour = colour_at(x, y, idx);
                auto* pixel = line + std::size_t(screen_x + x) * 4;
                pixel[0] = colour.r; pixel[1] = colour.g; pixel[2] = colour.b; pixel[3] = 0xFF;
            }
            continue;
        }
        for (int x = x_begin; x < x_end; ++x) {
            const std::uint8_t idx = row[x];
            if (idx == 0) continue;
            const auto colour = colour_at(x, y, idx);
            auto* pixel = line + std::size_t(screen_x + x) * 4;
            const int alpha = hole ? hole->alpha(screen_x + x, pixel_y, alpha_all) : alpha_all;
            if (alpha >= 255) { pixel[0] = colour.r; pixel[1] = colour.g; pixel[2] = colour.b; }
            else { pixel[0] = std::uint8_t((pixel[0] * (255 - alpha) + colour.r * alpha) / 255); pixel[1] = std::uint8_t((pixel[1] * (255 - alpha) + colour.g * alpha) / 255); pixel[2] = std::uint8_t((pixel[2] * (255 - alpha) + colour.b * alpha) / 255); }
            pixel[3] = 0xFF;
        }
    }
}

}  // namespace

void blit_dt1_tile(std::vector<std::uint8_t>& framebuffer,
                   const d2d::dt1::Tile& tile,
                   const d2d::palette::Palette& pal,
                   int screen_x, int screen_y, int alpha_all , const Hole* hole) {
    // Opaque through one palette (most floors): each index is a whole 32-bit
    // pixel, alpha 0xFF as blit_tile_pixels writes it, so one store a pixel.
    if (!hole && alpha_all >= 255) {
        const int screen_w = fb_width(framebuffer);
        const int x_begin = std::max(0, -screen_x), x_end = std::min(tile.width, screen_w - screen_x);
        const int y_begin = std::max(0, -screen_y), y_end = std::min(std::abs(tile.height), int(kScreenHeight) - screen_y);
        if (x_begin >= x_end) return;
        std::array<std::uint32_t, 256> lut{};
        const auto entries = pal.entries();
        for (std::size_t i = 0; i < 256; ++i) { std::memcpy(&lut[i], &entries[i], 4); lut[i] |= 0xFF000000u; }
        for (int y = y_begin; y < y_end; ++y) {
            const auto* row = tile.pixels.data() + std::size_t(y) * std::size_t(tile.width);
            auto* line = framebuffer.data() + (std::size_t(screen_y + y) * std::size_t(screen_w) + std::size_t(screen_x)) * 4;
            for (int x = x_begin; x < x_end; ++x) {
                const std::uint8_t idx = row[x];
                if (idx) std::memcpy(line + std::size_t(x) * 4, &lut[idx], 4);
            }
        }
        return;
    }
    blit_tile_pixels(framebuffer, tile, screen_x, screen_y, alpha_all, hole, [&](int, int, std::uint8_t idx) { return pal[idx]; });
}

void blit_dt1_shadow(std::vector<std::uint8_t>& framebuffer, const d2d::dt1::Tile& tile,
                     const d2d::palette::Palette& pal, int screen_x, int screen_y) {
    const int screen_w = fb_width(framebuffer);
    const int x_begin = std::max(0, -screen_x), x_end = std::min(tile.width, screen_w - screen_x);
    const int y_begin = std::max(0, -screen_y), y_end = std::min(std::abs(tile.height), int(kScreenHeight) - screen_y);
    if (x_begin >= x_end) return;
    for (int y = y_begin; y < y_end; ++y) {
        const auto* row = tile.pixels.data() + std::size_t(y) * std::size_t(tile.width);
        auto* line = framebuffer.data() + std::size_t(screen_y + y) * std::size_t(screen_w) * 4;
        for (int x = x_begin; x < x_end; ++x) {
            const std::uint8_t idx = row[x];
            if (idx == 0) continue;
            const auto colour = pal[idx];
            auto* pixel = line + std::size_t(screen_x + x) * 4;
            pixel[0] = std::uint8_t((pixel[0] + colour.r * 3) / 4); pixel[1] = std::uint8_t((pixel[1] + colour.g * 3) / 4); pixel[2] = std::uint8_t((pixel[2] + colour.b * 3) / 4);
        }
    }
}

void blit_dt1_tile_lit(std::vector<std::uint8_t>& framebuffer, const d2d::dt1::Tile& tile, const Lighting& light,
                       int screen_x, int screen_y, int cell_x, int cell_y, int alpha_all , const Hole* hole) {
    // A floor whose DT1 material has 0x100 is drawn at full light
    // (FUN_004de410: its table all 0xff; lava).
    if (tile.material_flags & 0x100) { blit_dt1_tile(framebuffer, tile, (*light.pal)[31], screen_x, screen_y, alpha_all, hole); return; }
    // FUN_004ddef0: an 8 x 8 table of the grid round the tile, entry (col,
    // row) the subtile (x - 1 + col, y - 1 + row) from the cell's.
    std::array<int, 64> table{};
    int low = 255, high = 0;
    for (int row = 0; row < 8; ++row)
        for (int col = 0; col < 8; ++col) {
            const int value = light.grid.at(cell_x * 5 - 1 + col, cell_y * 5 - 1 + row);
            table[std::size_t(row * 8 + col)] = value;
            low = std::min(low, value); high = std::max(high, value);
        }
    const auto& pals = *light.pal;
    if (low >> 3 == high >> 3) { blit_dt1_tile(framebuffer, tile, pals[std::size_t(low >> 3)], screen_x, screen_y, alpha_all, hole); return; }
    // Each 32 x 16 block (subtile (bx, by)) as the software driver's block
    // callback FUN_004f8b80 takes it: flat at A >> 3 when its four entries
    // barely differ, else four corner levels, each the mean of four entries
    // (the right one with game.exe's odd pick: C, not D), top-left,
    // top-right, bottom-right, bottom-left of the block; each row runs down
    // the left and right edges (FUN_004f8850), then across through the ramp
    // table 0x7ca320 (floor(L + (R - L) * u / 32)); A..D are entries 9, 10, 17, 18.
    struct Block { bool flat; int level, v0, v1, v2, v3; };
    std::array<Block, 25> blocks{};
    auto entry = [&](int row, int col) { return table[std::size_t(row * 8 + col)]; };
    for (int block_y = 0; block_y < 5; ++block_y)
        for (int block_x = 0; block_x < 5; ++block_x) {
            const int ent_a = entry(block_y + 1, block_x + 1), ent_b = entry(block_y + 1, block_x + 2), ent_c = entry(block_y + 2, block_x + 1), ent_d = entry(block_y + 2, block_x + 2);
            auto& block = blocks[std::size_t(block_y * 5 + block_x)];
            block.flat = std::abs(ent_b - ent_a) + std::abs(ent_c - ent_a) + std::abs(ent_d - ent_b) < 10;
            block.level = ent_a >> 3;
            block.v0 = (ent_c + entry(block_y + 2, block_x) + ent_a + entry(block_y + 1, block_x)) >> 2;
            block.v1 = (entry(block_y, block_x + 1) + entry(block_y, block_x + 2) + ent_b + ent_a) >> 2;
            block.v2 = (entry(block_y + 2, block_x + 3) + ent_c + entry(block_y + 1, block_x + 3) + ent_b) >> 2;
            block.v3 = (entry(block_y + 3, block_x + 1) + entry(block_y + 3, block_x + 2) + ent_d + ent_c) >> 2;
        }
    auto level = [&](int offset_x, int offset_y) {
        // The subtile under the pixel (its diamond's block), then where in
        // the block's rectangle it lies. Tall floors' and roofs' pixels past
        // the 5 x 5 take the nearest block. floor((x / 16 + y / 8) / 2) is
        // (x + 2y) >> 5 exactly; a ramp's floor(step / 32) is step >> 5.
        const int block_x = std::clamp((offset_x + 2 * offset_y) >> 5, 0, 4);
        const int block_y = std::clamp((2 * offset_y - offset_x) >> 5, 0, 4);
        const auto& block = blocks[std::size_t(block_y * 5 + block_x)];
        if (block.flat) return std::size_t(block.level);
        const int along = std::clamp(offset_x - ((block_x - block_y) * 16 - 16), 0, 31), row_in = std::clamp(offset_y - (block_x + block_y) * 8, 0, 15);
        const int left = (block.v0 * 16 + (block.v3 - block.v0) * row_in) >> 7, right = (block.v1 * 16 + (block.v2 - block.v1) * row_in) >> 7;
        return std::size_t(std::clamp(left + (((right - left) * along) >> 5), 0, 31));
    };
    blit_tile_pixels(framebuffer, tile, screen_x, screen_y, alpha_all, hole, [&](int x, int y, std::uint8_t idx) {
        return pals[level(x - kIsoW / 2, y - tile.y_shift)][idx];   // from the diamond's top: block (0, 0) at x 64, y 0
    });
}

void blit_dt1_wall_lit(std::vector<std::uint8_t>& framebuffer, const d2d::dt1::Tile& tile, const Lighting& light,
                       int screen_x, int screen_y, int cell_x, int cell_y, bool in_front, int alpha_all, const Hole* hole) {
    // 0x72a9e8 (0x72abc8 for a wall in front) by the tile's direction, filled
    // at start-up (read live): subtiles from the cell's top corner, one per
    // 32-pixel column edge.
    using Points = std::array<std::array<int, 2>, 6>;
    static constexpr std::array<Points, 5> kPoints{ {
        { { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } } },
        { { { 2, 5 }, { 2, 3 }, { 2, 1 }, { 2, -1 }, { 2, -3 }, { 2, -5 } } },
        { { { -5, 2 }, { -3, 2 }, { -1, 2 }, { 1, 2 }, { 3, 2 }, { 5, 2 } } },
        { { { 2, 5 }, { 2, 3 }, { 2, 2 }, { 2, 2 }, { 3, 2 }, { 5, 2 } } },
        { { { -5, 2 }, { -3, 2 }, { -1, 2 }, { 2, -1 }, { 2, -3 }, { 2, -5 } } },
    } };
    static constexpr std::array<Points, 5> kFrontPoints{ {
        { { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } } },
        { { { -2, 5 }, { -2, 3 }, { -2, 1 }, { -2, -1 }, { -2, -3 }, { -2, -5 } } },
        { { { -5, -2 }, { -3, -2 }, { -1, -2 }, { 1, -2 }, { 3, -2 }, { 5, -2 } } },
        { { { -2, 5 }, { -2, 3 }, { -2, -2 }, { -2, -2 }, { 3, -2 }, { 5, -2 } } },
        { { { -5, -2 }, { -3, -2 }, { -1, -2 }, { -2, -1 }, { -2, -3 }, { -2, -5 } } },
    } };
    const auto& points = (in_front ? kFrontPoints : kPoints)[std::size_t(tile.direction >= 1 && tile.direction <= 4 ? tile.direction : 0)];
    std::array<int, 6> lights{};
    int low = 255, high = 0;
    for (std::size_t k = 0; k < lights.size(); ++k) {
        lights[k] = light.grid.at(cell_x * 5 + points[k][0], cell_y * 5 + points[k][1]);
        low = std::min(low, lights[k]); high = std::max(high, lights[k]);
    }
    const auto& pals = *light.pal;
    if (low >> 3 == high >> 3) { blit_dt1_tile(framebuffer, tile, pals[std::size_t(low >> 3)], screen_x, screen_y, alpha_all, hole); return; }
    // A block (FUN_004f8120): flat at the left light's level when its two
    // lights differ by under 10, else the ramp between their levels.
    blit_tile_pixels(framebuffer, tile, screen_x, screen_y, alpha_all, hole, [&](int x, int, std::uint8_t idx) {
        const int column = std::min(x / 32, 4), along = x - column * 32;
        const int left = lights[std::size_t(column)], right = lights[std::size_t(column) + 1];
        const int lvl = std::abs(right - left) < 10 ? left >> 3 : (left >> 3) + ((((right >> 3) - (left >> 3)) * along) >> 5);
        return pals[std::size_t(lvl)][idx];
    });
}

}  // namespace d2d::client
