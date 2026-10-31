// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game dt1 palette font dc6
// test-sources: apps/d2d/tile_blit.cpp
// test-includes: apps/d2d
// The DT1 tile blitters (tile_blit.cpp) against a copy of their first,
// per-pixel form: the same bytes for every tile, clip, alpha, hole and
// light grid. `test_tile_blit --bench` times both.
#include "common.hpp"
#include "tile_blit.hpp"

#include <dt1.hpp>
#include <light.hpp>
#include <palette.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ratio>
#include <vector>

namespace d2d::client::reference {

void blit_dt1_tile(std::vector<std::uint8_t>& framebuffer,
                   const d2d::dt1::Tile& tile,
                   const d2d::palette::Palette& pal,
                   int screen_x, int screen_y, int alpha_all , const Hole* hole) {
    const int tile_height = std::abs(tile.height);
    const int screen_w = fb_width(framebuffer);
    for (int y = 0; y < tile_height; ++y) {
        const int pixel_y = screen_y + y;
        if (pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
        const auto* row = tile.pixels.data() + std::size_t(y) * tile.width;
        for (int x = 0; x < tile.width; ++x) {
            const std::uint8_t idx = row[x];
            if (idx == 0) continue;   // transparent
            const int pixel_x = screen_x + x;
            if (pixel_x < 0 || pixel_x >= screen_w) continue;
            const auto colour = pal[idx];
            auto* pixel = framebuffer.data() + (std::size_t(pixel_y) * std::size_t(screen_w) + std::size_t(pixel_x)) * 4;
            const int alpha = hole ? hole->alpha(pixel_x, pixel_y, alpha_all) : alpha_all;
            if (alpha >= 255) { pixel[0] = colour.r; pixel[1] = colour.g; pixel[2] = colour.b; }
            else { pixel[0] = std::uint8_t((pixel[0] * (255 - alpha) + colour.r * alpha) / 255); pixel[1] = std::uint8_t((pixel[1] * (255 - alpha) + colour.g * alpha) / 255); pixel[2] = std::uint8_t((pixel[2] * (255 - alpha) + colour.b * alpha) / 255); }
            pixel[3] = 0xFF;
        }
    }
}

void blit_dt1_shadow(std::vector<std::uint8_t>& framebuffer, const d2d::dt1::Tile& tile,
                     const d2d::palette::Palette& pal, int screen_x, int screen_y) {
    const int tile_height = std::abs(tile.height);
    const int screen_w = fb_width(framebuffer);
    for (int y = 0; y < tile_height; ++y) {
        const int pixel_y = screen_y + y;
        if (pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
        const auto* row = tile.pixels.data() + std::size_t(y) * tile.width;
        for (int x = 0; x < tile.width; ++x) {
            const std::uint8_t idx = row[x];
            const int pixel_x = screen_x + x;
            if (idx == 0 || pixel_x < 0 || pixel_x >= screen_w) continue;
            const auto colour = pal[idx];
            auto* pixel = framebuffer.data() + (std::size_t(pixel_y) * std::size_t(screen_w) + std::size_t(pixel_x)) * 4;
            pixel[0] = std::uint8_t((pixel[0] + colour.r * 3) / 4); pixel[1] = std::uint8_t((pixel[1] + colour.g * 3) / 4); pixel[2] = std::uint8_t((pixel[2] + colour.b * 3) / 4);
        }
    }
}

void blit_dt1_tile_lit(std::vector<std::uint8_t>& framebuffer, const d2d::dt1::Tile& tile, const Lighting& light,
                       int screen_x, int screen_y, int cell_x, int cell_y, int alpha_all , const Hole* hole) {
    // A floor whose DT1 material has 0x100 is drawn at full light
    // (FUN_004de410: its table all 0xff; lava).
    if (tile.material_flags & 0x100) { reference::blit_dt1_tile(framebuffer, tile, (*light.pal)[31], screen_x, screen_y, alpha_all, hole); return; }
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
    if (low >> 3 == high >> 3) { reference::blit_dt1_tile(framebuffer, tile, pals[std::size_t(low >> 3)], screen_x, screen_y, alpha_all, hole); return; }
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
        // the 5 x 5 take the nearest block.
        const int block_x = std::clamp(int(std::floor((float(offset_x) / 16.f + float(offset_y) / 8.f) / 2.f)), 0, 4);
        const int block_y = std::clamp(int(std::floor((float(offset_y) / 8.f - float(offset_x) / 16.f) / 2.f)), 0, 4);
        const auto& block = blocks[std::size_t(block_y * 5 + block_x)];
        if (block.flat) return std::size_t(block.level);
        const int along = std::clamp(offset_x - ((block_x - block_y) * 16 - 16), 0, 31), row_in = std::clamp(offset_y - (block_x + block_y) * 8, 0, 15);
        const int left = (block.v0 * 16 + (block.v3 - block.v0) * row_in) >> 7, right = (block.v1 * 16 + (block.v2 - block.v1) * row_in) >> 7;
        const int step = (right - left) * along;
        return std::size_t(std::clamp(left + (step >= 0 ? step / 32 : -((-step + 31) / 32)), 0, 31));
    };
    const int tile_height = std::abs(tile.height);
    const int screen_w = fb_width(framebuffer);
    for (int y = 0; y < tile_height; ++y) {
        const int pixel_y = screen_y + y;
        if (pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
        const auto* row = tile.pixels.data() + std::size_t(y) * tile.width;
        for (int x = 0; x < tile.width; ++x) {
            const std::uint8_t idx = row[x];
            if (idx == 0) continue;
            const int pixel_x = screen_x + x;
            if (pixel_x < 0 || pixel_x >= screen_w) continue;
            const auto lvl = level(x - kIsoW / 2, y - tile.y_shift);   // from the diamond's top: block (0, 0) at x 64, y 0
            const auto col = pals[lvl][idx];
            auto* pixel = framebuffer.data() + (std::size_t(pixel_y) * std::size_t(screen_w) + std::size_t(pixel_x)) * 4;
            const int alpha = hole ? hole->alpha(pixel_x, pixel_y, alpha_all) : alpha_all;
            if (alpha >= 255) { pixel[0] = col.r; pixel[1] = col.g; pixel[2] = col.b; }
            else { pixel[0] = std::uint8_t((pixel[0] * (255 - alpha) + col.r * alpha) / 255); pixel[1] = std::uint8_t((pixel[1] * (255 - alpha) + col.g * alpha) / 255); pixel[2] = std::uint8_t((pixel[2] * (255 - alpha) + col.b * alpha) / 255); }
            pixel[3] = 0xFF;
        }
    }
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
    if (low >> 3 == high >> 3) { reference::blit_dt1_tile(framebuffer, tile, pals[std::size_t(low >> 3)], screen_x, screen_y, alpha_all, hole); return; }
    const int tile_height = std::abs(tile.height);
    const int screen_w = fb_width(framebuffer);
    for (int y = 0; y < tile_height; ++y) {
        const int pixel_y = screen_y + y;
        if (pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
        const auto* row = tile.pixels.data() + std::size_t(y) * tile.width;
        for (int x = 0; x < tile.width; ++x) {
            const std::uint8_t idx = row[x];
            const int pixel_x = screen_x + x;
            if (idx == 0 || pixel_x < 0 || pixel_x >= screen_w) continue;
            // A block (FUN_004f8120): flat at the left light's level when its
            // two lights differ by under 10, else the ramp between their levels.
            const int column = std::min(x / 32, 4), along = x - column * 32;
            const int left = lights[std::size_t(column)], right = lights[std::size_t(column) + 1];
            const int step = ((right >> 3) - (left >> 3)) * along;
            const int lvl = std::abs(right - left) < 10 ? left >> 3 : (left >> 3) + (step >= 0 ? step / 32 : -((-step + 31) / 32));
            const auto col = pals[std::size_t(lvl)][idx];
            auto* pixel = framebuffer.data() + (std::size_t(pixel_y) * std::size_t(screen_w) + std::size_t(pixel_x)) * 4;
            const int alpha = hole ? hole->alpha(pixel_x, pixel_y, alpha_all) : alpha_all;
            if (alpha >= 255) { pixel[0] = col.r; pixel[1] = col.g; pixel[2] = col.b; }
            else { pixel[0] = std::uint8_t((pixel[0] * (255 - alpha) + col.r * alpha) / 255); pixel[1] = std::uint8_t((pixel[1] * (255 - alpha) + col.g * alpha) / 255); pixel[2] = std::uint8_t((pixel[2] * (255 - alpha) + col.b * alpha) / 255); }
            pixel[3] = 0xFF;
        }
    }
}

}  // namespace d2d::client::reference

using namespace d2d::client;

namespace {

// A fixed generator: the same cases every run.
struct Random {
    std::uint32_t state;
    std::uint32_t next() { state = state * 1664525u + 1013904223u; return state >> 8; }
    int range(int low, int high) { return low + int(next() % std::uint32_t(high - low + 1)); }
};

std::array<d2d::palette::Palette, 32> make_palettes(Random& random) {
    std::array<d2d::palette::Palette, 32> pals;
    for (auto& pal : pals) {
        std::array<d2d::palette::Rgba, 256> entries{};
        for (auto& entry : entries) entry = { std::uint8_t(random.next()), std::uint8_t(random.next()), std::uint8_t(random.next()), 0xFF };
        pal = d2d::palette::Palette(entries);
    }
    return pals;
}

// A tile: index 0 (transparent) in runs and specks, as decoded DT1s have.
d2d::dt1::Tile make_tile(Random& random, int width, int height, int y_shift) {
    d2d::dt1::Tile tile;
    tile.width = width; tile.height = height; tile.y_shift = y_shift;
    tile.pixels.resize(std::size_t(width) * std::size_t(std::abs(height)));
    for (int row = 0; row < std::abs(height); ++row) {
        const int left = random.range(0, width / 2), right = random.range(width / 2, width);
        for (int col = 0; col < width; ++col)
            tile.pixels[std::size_t(row * width + col)] = col < left || col >= right || random.range(0, 15) == 0 ? 0 : std::uint8_t(random.range(1, 255));
    }
    return tile;
}

// A light grid: flat stretches, smooth ramps and noise, so both the
// one-level and the per-block paths run.
d2d::rules::LightGrid make_grid(Random& random) {
    d2d::rules::LightGrid grid;
    grid.origin_x = random.range(-20, 20); grid.origin_y = random.range(-20, 20);
    const int kind = random.range(0, 2);
    for (int row = 0; row < grid.grid_size; ++row)
        for (int col = 0; col < grid.grid_size; ++col)
            grid.values[std::size_t(row * grid.grid_size + col)] = std::uint8_t(
                kind == 0 ? 100 + random.range(0, 3) : kind == 1 ? std::clamp(col * 6 + row * 3 - 40, 0, 255) : random.range(0, 255));
    return grid;
}

std::vector<std::uint8_t> make_framebuffer(Random& random, int width) {
    std::vector<std::uint8_t> framebuffer(std::size_t(width) * kScreenHeight * 4);
    for (auto& byte : framebuffer) byte = std::uint8_t(random.next());
    return framebuffer;
}

}  // namespace

int main(int argc, char** argv) {
    Random random{ 12345 };
    const auto pals = make_palettes(random);
    int cases = 0;
    for (int round = 0; round < 3000; ++round) {
        const int width = round % 3 == 0 ? 1068 : int(kScreenWidth);
        auto expected = make_framebuffer(random, width);
        auto actual = expected;
        // Floors (160 x 80 at y_shift 0), roofs and tall floors (160 x 128,
        // y_shift 48), walls of any height, a narrow odd tile.
        const int shape = random.range(0, 3);
        const int tile_w = shape == 3 ? random.range(1, 40) : 160;
        const int tile_h = shape == 0 ? -80 : shape == 1 ? 128 : random.range(1, 260);
        auto tile = make_tile(random, tile_w, tile_h, shape == 1 ? 48 : random.range(0, 40));
        tile.material_flags = random.range(0, 9) == 0 ? 0x100 : 0;
        tile.direction = random.range(0, 5);
        const int screen_x = random.range(-220, width + 20), screen_y = random.range(-300, int(kScreenHeight) + 20);
        const int alpha = std::array{ 255, 255, 128, 0, 300, 200 }[std::size_t(random.range(0, 5))];
        Hole hole;
        for (int count = random.range(1, 2); count > 0; --count) hole.centres.emplace_back(random.range(0, width), random.range(0, int(kScreenHeight)));
        hole.radius = random.range(20, 120);
        const Hole* hole_ptr = random.range(0, 2) == 0 ? &hole : nullptr;
        Lighting light;
        light.grid = make_grid(random);
        light.pal = &pals;
        const int cell_x = random.range(-6, 14), cell_y = random.range(-6, 14);
        const auto& pal = pals[std::size_t(random.range(0, 31))];
        switch (round % 4) {
        case 0:
            reference::blit_dt1_tile(expected, tile, pal, screen_x, screen_y, alpha, hole_ptr);
            blit_dt1_tile(actual, tile, pal, screen_x, screen_y, alpha, hole_ptr);
            break;
        case 1:
            reference::blit_dt1_shadow(expected, tile, pal, screen_x, screen_y);
            blit_dt1_shadow(actual, tile, pal, screen_x, screen_y);
            break;
        case 2:
            reference::blit_dt1_tile_lit(expected, tile, light, screen_x, screen_y, cell_x, cell_y, alpha, hole_ptr);
            blit_dt1_tile_lit(actual, tile, light, screen_x, screen_y, cell_x, cell_y, alpha, hole_ptr);
            break;
        default:
            reference::blit_dt1_wall_lit(expected, tile, light, screen_x, screen_y, cell_x, cell_y, alpha < 255, alpha, hole_ptr);
            blit_dt1_wall_lit(actual, tile, light, screen_x, screen_y, cell_x, cell_y, alpha < 255, alpha, hole_ptr);
            break;
        }
        if (expected != actual) {
            std::fprintf(stderr, "round %d (kind %d) differs: tile %dx%d at (%d, %d) alpha %d hole %d\n",
                         round, round % 4, tile_w, tile_h, screen_x, screen_y, alpha, hole_ptr != nullptr);
            return 1;
        }
        ++cases;
    }
    // Fixed edges the rolls don't reach: an empty tile, one taller than the
    // screen (clipped top and bottom), one wider than the framebuffer.
    struct Edge { int width, height, screen_x, screen_y; };
    for (const auto& edge : { Edge{ 0, 80, 10, 10 }, Edge{ 160, 0, 10, 10 }, Edge{ 160, 700, 30, -50 }, Edge{ 900, 90, -50, 20 }, Edge{ 900, 700, -50, -50 } }) {
        auto expected = make_framebuffer(random, int(kScreenWidth));
        auto actual = expected;
        const auto tile = make_tile(random, edge.width, edge.height, 0);
        Lighting light;
        light.grid = make_grid(random);
        light.pal = &pals;
        reference::blit_dt1_tile(expected, tile, pals[3], edge.screen_x, edge.screen_y, 255, nullptr);
        blit_dt1_tile(actual, tile, pals[3], edge.screen_x, edge.screen_y);
        reference::blit_dt1_shadow(expected, tile, pals[4], edge.screen_x, edge.screen_y);
        blit_dt1_shadow(actual, tile, pals[4], edge.screen_x, edge.screen_y);
        reference::blit_dt1_tile_lit(expected, tile, light, edge.screen_x, edge.screen_y, 2, 2, 128, nullptr);
        blit_dt1_tile_lit(actual, tile, light, edge.screen_x, edge.screen_y, 2, 2, 128);
        if (expected != actual) {
            std::fprintf(stderr, "edge tile %dx%d at (%d, %d) differs\n", edge.width, edge.height, edge.screen_x, edge.screen_y);
            return 1;
        }
        ++cases;
    }
    std::printf("%d cases match\n", cases);

    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0) {
        // A town-like screen: 400 lit 160 x 80 floors under a ramped grid,
        // 150 walls, timed a frame at a time over many frames.
        Lighting light;
        Random bench_random{ 777 };
        light.grid = make_grid(bench_random);
        for (int row = 0; row < light.grid.grid_size; ++row)
            for (int col = 0; col < light.grid.grid_size; ++col)
                light.grid.values[std::size_t(row * light.grid.grid_size + col)] = std::uint8_t(std::clamp(80 + col * 4 - row * 2, 0, 255));
        light.grid.origin_x = 0; light.grid.origin_y = 0;
        light.pal = &pals;
        const auto floor = make_tile(bench_random, 160, -80, 0);
        const auto wall = make_tile(bench_random, 160, 192, 0);
        auto framebuffer = make_framebuffer(bench_random, int(kScreenWidth));
        auto frame = [&](auto&& floor_blit, auto&& wall_blit) {
            for (int index = 0; index < 400; ++index)
                floor_blit(framebuffer, floor, light, (index % 20) * 48 - 80, (index / 20) * 32 - 40, index % 9, index / 9 % 9);
            for (int index = 0; index < 150; ++index)
                wall_blit(framebuffer, wall, light, (index % 15) * 60 - 60, (index / 15) * 64 - 100, index % 9, index / 9 % 9);
        };
        auto time = [&](const char* name, auto&& floor_blit, auto&& wall_blit) {
            for (int warm = 0; warm < 5; ++warm) frame(floor_blit, wall_blit);
            const auto start = std::chrono::steady_clock::now();
            constexpr int kFrames = 100;
            for (int count = 0; count < kFrames; ++count) frame(floor_blit, wall_blit);
            const double ms_per_frame = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / kFrames;
            std::printf("%-10s %.3f ms/frame (checksum %u)\n", name, ms_per_frame, unsigned(framebuffer[12345]));
        };
        auto reference_floor = [](auto& target, const auto& tile, const auto& lit, int x, int y, int cell_x, int cell_y) { reference::blit_dt1_tile_lit(target, tile, lit, x, y, cell_x, cell_y, 255, nullptr); };
        auto reference_wall = [](auto& target, const auto& tile, const auto& lit, int x, int y, int cell_x, int cell_y) { reference::blit_dt1_wall_lit(target, tile, lit, x, y, cell_x, cell_y, false, 255, nullptr); };
        auto current_floor = [](auto& target, const auto& tile, const auto& lit, int x, int y, int cell_x, int cell_y) { blit_dt1_tile_lit(target, tile, lit, x, y, cell_x, cell_y); };
        auto current_wall = [](auto& target, const auto& tile, const auto& lit, int x, int y, int cell_x, int cell_y) { blit_dt1_wall_lit(target, tile, lit, x, y, cell_x, cell_y, false); };
        time("reference", reference_floor, reference_wall);
        time("current", current_floor, current_wall);
    }
    return 0;
}
