// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for world_view.hpp: drawing the world: tiles, walls, units, lights.
#include "world_view.hpp"

#include "common.hpp"
#include "scene.hpp"
#include "watchdog.hpp"

#include <cof.hpp>
#include <dc6.hpp>
#include <dt1.hpp>
#include <palette.hpp>
#include <overlays.hpp>
#include <room_tiles.hpp>
#include <weather.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::client {

namespace {

// An object's time into its mode, as the client steps its frame: a counter
// (8.8) from Start at FrameDelta a tick (FUN_00624390), held on FrameCnt - 1
// (CycleAnim 0) or back to Start past FrameCnt; FrameCnt 1 never moves
// (FUN_004bcbb0). The time it returns lands the anim's own rate on that frame.
std::uint32_t object_elapsed(const Unit& unit, std::uint32_t elapsed) {
    if (!unit.frames || !unit.anim) return elapsed;
    const std::uint32_t frames = unit.frames, start = unit.loop_from;
    const std::uint64_t counter = std::uint64_t(start) * 256u + std::uint64_t(elapsed / 40u) * unit.frame_delta;
    std::uint64_t frame = start;
    if (frames > 1 && start < frames - 1u) {
        if (unit.hold) frame = std::min<std::uint64_t>(counter / 256u, frames - 1u);
        else if (counter < std::uint64_t(frames) * 256u) frame = counter / 256u;
        else frame = (start * 256u + (counter - start * 256u) % ((frames - start) * 256u)) / 256u;
    }
    return std::uint32_t(frame) * std::max<std::uint32_t>(unit.anim->ms_per_frame(), 1);
}

// An object's FrameCnt past its COF's frames (the Act 3 magic shrine: 13 in a 1-frame COF):
// game.exe's frame runs on, its DCCs have no cell there (FUN_005feb80), nothing draws, its shadow neither.
// ponytail: blank outright, not the COF priority game.exe reads past its table.
bool past_cof(const Unit& unit, std::uint32_t elapsed) {
    return unit.frames && elapsed / std::max<std::uint32_t>(unit.anim->ms_per_frame(), 1) >= std::uint32_t(unit.anim->cof.frames_per_direction());
}

}  // namespace

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
    if (low >> 3 == high >> 3) { blit_dt1_tile(framebuffer, tile, pals[std::size_t(low >> 3)], screen_x, screen_y, alpha_all, hole); return; }
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

const d2d::dc6::Frame* flippy_frame(const d2d::dc6::Sprite& sprite, std::uint32_t elapsed) {
    if (sprite.directions() == 0 || sprite.frames_per_direction() == 0) return nullptr;
    return &sprite.frame(0, std::min<std::uint32_t>(elapsed / 40, sprite.frames_per_direction() - 1));
}

std::array<int, 4> composite_bounds(const Scene::PlayerAnim& anim, int dir_want,
                                    std::uint32_t elapsed_ms, int anchor_x, int anchor_y) {
    std::array<int, 4> bounds{ INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN };
    const auto dirs = anim.cof.directions(), fpd = anim.cof.frames_per_direction();
    if (dirs == 0 || fpd == 0) return bounds;
    const auto dir = cof_direction(dir_want, dirs);
    // 25 ticks/s; each tick advances speed/256 frames.
    const auto ms_per_frame = anim.ms_per_frame();
    const auto frame = std::uint8_t((elapsed_ms / ms_per_frame) % fpd);
    for (std::size_t layer_index = 0; layer_index < anim.dcc.size(); ++layer_index) {
        const auto& spr = anim.layer(layer_index);
        const auto layer_dir = spr.directions() == dirs ? dir : cof_direction(dir_want, spr.directions());   // as composite_frames
        if (layer_dir >= spr.directions() || frame >= spr.frames_per_direction()) continue;
        const auto& frame_ref = spr.frame(layer_dir, frame);
        bounds = { std::min(bounds[0], anchor_x + frame_ref.box_left), std::min(bounds[1], anchor_y + frame_ref.box_top),
              std::max(bounds[2], anchor_x + frame_ref.box_right), std::max(bounds[3], anchor_y + frame_ref.box_bottom) };
    }
    return bounds;
}

void render_world(std::vector<std::uint8_t>& framebuffer,
                  const Scene& scene,
                  const Level& level,
                  float cam_x, float cam_y,
                  std::uint32_t elapsed_ms ,
                  std::span<const Unit> units ,
                  int mouse_x , int mouse_y ,
                  std::pair<const Unit*, std::array<int, 4>>* hovered ,
                  const Lighting* light , d2d::rules::Rain* rain ,
                  std::vector<std::pair<const Unit*, std::array<int, 4>>>* items) {   // each ground item drawn, its box
    const auto& map = level.ds1;
    if (map.width() == 0 || map.height() == 0) return;
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    const int screen_w = fb_width(framebuffer);
    const int cx0 = screen_w / 2;
    g_drawn.level = &level;
    g_drawn.cells.clear();
    const int map_width  = map.width();
    const int base_x = int(std::floor(cam_x)), base_y = int(std::floor(cam_y));
    // Screen position of cell (gx, gy)'s tile point. A floor's 128-row
    // buffer is blitted 48 rows above it (blit_cell), so its diamond's top
    // lands on kFloorViewY when the camera sits on the cell's corner.
    auto iso = [&](int cell_x, int cell_y) {
        const float dx = float(cell_x) - cam_x, dy = float(cell_y) - cam_y;
        return std::pair{ cx0 + int(std::lround((dx - dy) * (kIsoW / 2))),
                          kFloorViewY + 48 + int(std::lround((dx + dy) * (kIsoH / 2))) };
    };
    // Screen position of a continuous world point (a unit's feet): the
    // camera point lands at (kW/2, kViewY).
    auto iso_point = [&](float x, float y) {
        const float dx = x - cam_x, dy = y - cam_y;
        return std::pair{ cx0 + int(std::lround((dx - dy) * (kIsoW / 2))),
                          kViewY + int(std::lround((dx + dy) * (kIsoH / 2))) };
    };

    // Iso footprint for the 800x600 window: each screen cell is 160x80.
    // A ±12 grid-cell window around the camera covers > 2× screen area,
    // leaving room for tall walls (up to 128+px) to reach in from cells
    // that are off-screen at their base.
    constexpr int kReach = 12;

    // Blit a single tile at cell (gx, gy)'s iso position, honouring the
    // 80-tall-diamond-at-bottom convention shared by floor/wall pixel
    // buffers.
    // Drawn, as game.exe sets tile flag 0x20000: a floor or roof whose
    // spot (its buffer's top left) is in the view's box {-0x50, -0x50,
    // W + 0x50, H - 0x2f} (FUN_00476000; tested in FUN_004de410 and
    // FUN_004dea70); a wall with a 32 px block whose top left is in
    // {-0x20, -0x20, W, H + 0x20}, a side panel taking its half
    // (FUN_005130a0, the driver's wall draw).
    auto floor_drawn = [&](int screen_x, int screen_y) {
        return screen_x >= -0x50 && screen_x < screen_w + 0x50 && screen_y >= -0x50 && screen_y < int(kScreenHeight) - 0x2f;
    };
    auto wall_drawn = [&](int screen_x, int screen_y, const d2d::dt1::Tile& tile) {
        const int left = g_drawn.panel_side == 2 ? screen_w / 2 - 0x20 : -0x20;
        const int right = g_drawn.panel_side == 1 ? screen_w - screen_w / 2 : screen_w;
        return std::ranges::any_of(tile.blocks, [&](const auto& block) {
            const int block_x = screen_x + block.first, block_y = screen_y + block.second;
            return block_x >= left && block_x < right && block_y > -0x21 && block_y < int(kScreenHeight) + 0x20;
        });
    };
    // `layer`: 0 wall, 1 floor, 2 shadow (the pass, not the DT1's own type field).
    // `source`: the Level::Pick or DS1 layer cell it's drawn for (g_drawn).
    auto blit_cell = [&](int cell_x, int cell_y, const d2d::dt1::Tile& tile, int layer, const void* source, int alpha = 255) -> bool {
        const auto [iso_x, iso_y] = iso(cell_x, cell_y);
        const int tile_height = std::abs(tile.height);
        const int screen_x = iso_x - kIsoW / 2;         // block x counts from the cell's left corner, whatever the tile's width
        // A wall's block y counts from the cell's bottom corner (iso_y + 80):
        // upper walls rise above it, lower walls (16..19: Act 5's cliffs) hang below.
        // Walls sit 16 px higher than a floor's frame would put them
        // (measured live against game.exe in Lut Gholein: floors, roofs,
        // units and walls each on game.exe's pixels; not traced).
        const int wall_lift = layer == 0 ? 16 : 0;
        const int screen_y = (layer == 0 && tile.type >= 16 ? iso_y + kIsoH - tile.y_shift : iso_y - (tile_height - kIsoH)) - wall_lift;
        const bool drawn = layer == 1 ? floor_drawn(screen_x, screen_y) : layer == 0 && wall_drawn(screen_x, screen_y, tile);
        if (drawn) g_drawn.cells.push_back({ cell_x, cell_y, source });
        if (layer == 2) blit_dt1_shadow(framebuffer, tile, pal, screen_x, screen_y);
        else if (light && layer == 0) blit_dt1_wall_lit(framebuffer, tile, *light, screen_x, screen_y, cell_x, cell_y, alpha < 255, alpha);   // ponytail: "in front" read off the alpha, so a wall fading back still samples the front spots
        else if (light) blit_dt1_tile_lit(framebuffer, tile, *light, screen_x, screen_y, cell_x, cell_y, alpha);
        else blit_dt1_tile(framebuffer, tile, pal, screen_x, screen_y, alpha);
        return drawn;
    };
    // Walls in front of the player see-through (FUN_004dd060 /
    // FUN_004dd180, the room mode: DAT_0072a968 is 1 in 1.14d and nothing
    // writes it): a wall of a Logicals preset's area (FUN_0066d110) other
    // than the player's, the player's cell before the area's top-left in x
    // or y, fades to alpha 0x80 over 500 ms, and back to 0xff once it isn't.
    // Any other room is one area and its walls never fade (FUN_0066c9c0:
    // +0x10 stays 0), so no town's do. Roofs (15) and lower walls (16..19)
    // never fade.
    // ponytail: the blend is linear in RGB, not the driver's alpha table;
    // a wall on a room's far edge takes the room it opens (room_of), and
    // tile flag 4 (never fades) isn't kept on the picks.
    struct Fade { int from = 255, target = 255; std::uint32_t changed_at = 0; };
    static std::unordered_map<std::uint64_t, Fade> fades;
    // The players (a player's Height2 units; the camera's point without one):
    // walls before them fade, roofs over them open.
    std::vector<std::pair<float, float>> players;
    for (const auto& unit : units) if (unit.overlay_class == 1) players.emplace_back(unit.x, unit.y);
    if (players.empty()) players.emplace_back(cam_x, cam_y);
    Hole hole{ {}, 70 };                                  // round each player's body
    for (const auto& [player_x, player_y] : players) {
        const auto [hole_x, hole_y] = iso_point(player_x, player_y);
        hole.at.emplace_back(hole_x, hole_y - 40);
    }
    // The area record at a cell (FUN_0066ce30's grid): null in a room
    // without Logicals (its one area has no rect).
    auto area_at = [](const Level& holder, int x, int y) -> const d2d::drlg::Area* {
        const int room = d2d::game::room_of(holder, float(x) + 0.5f, float(y) + 0.5f);
        if (room < 0 || std::size_t(room) >= holder.room_areas.size()) return nullptr;
        for (const auto& area : holder.room_areas[std::size_t(room)])
            if (x >= area.full_left && y >= area.full_top && x < area.full_right && y < area.full_bottom) return &area;
        return nullptr;
    };
    struct Standing { int x, y; std::uint32_t id; };
    std::vector<Standing> standing;                       // each player's cell and area id (DAT_007c8a08 / 0c / 10)
    for (const auto& [player_x, player_y] : players) {
        const int pcx = int(std::floor(player_x)), pcy = int(std::floor(player_y));
        const auto* area = area_at(level, pcx, pcy);
        standing.push_back({ pcx, pcy, area ? area->id : 1u });
    }
    auto wall_alpha = [&](const Level* wall_level, int off, int type, int wall_index, int cell_x, int cell_y) {
        const void* level_key = wall_level;
        int shift_x = 0, shift_y = 0;                     // a neighbour level's cells from this one's
        for (const auto& neighbour : level.nearby)
            if (neighbour.level == wall_level) { shift_x = neighbour.dx; shift_y = neighbour.dy; }
        const auto* area = type < 16 ? area_at(*wall_level, cell_x - shift_x, cell_y - shift_y) : nullptr;
        const bool see = area && std::ranges::any_of(standing, [&](const Standing& player) {
            return area->id != player.id && (player.x < area->full_left + shift_x || player.y < area->full_top + shift_y);
        });
        const int want = see ? 0x80 : 0xff;
        const auto key = std::uint64_t(reinterpret_cast<std::uintptr_t>(level_key)) * 1000003u ^ (std::uint64_t(off) << 12 | std::uint64_t(type) << 6 | std::uint64_t(wall_index));
        auto found = fades.find(key);
        if (found == fades.end()) { if (!see) return 255; found = fades.emplace(key, Fade{ 255, 255, elapsed_ms }).first; }
        auto& fade = found->second;
        const auto now_a = [&] {                // 0x7f every 500 ms, from where it was
            const int step = int(std::min<std::uint32_t>(elapsed_ms - fade.changed_at, 1000)) * 0x7f / 500;
            return fade.target > fade.from ? std::min(fade.target, fade.from + step) : std::max(fade.target, fade.from - step);
        };
        if (fade.target != want) { fade.from = now_a(); fade.target = want; fade.changed_at = elapsed_ms; }
        const int fade_alpha = now_a();
        if (fade_alpha >= 255 && !see) fades.erase(found);
        return fade_alpha;
    };

    auto find_tile = [&](const Level& tile_level, int style, int seq, int type)
        -> const d2d::dt1::Tile* {
        const auto found = tile_level.tile_lookup.find(tile_key(style, seq, type));
        return found == tile_level.tile_lookup.end() ? nullptr : found->second;
    };
    // Cell (gx, gy) of this level or, past its edge, of the level next to
    // it in the act (Level::nearby): D2 draws the neighbour's rooms too.
    auto cell_at = [&](int cell_x, int cell_y) -> std::pair<const Level*, std::size_t> {
        auto inside = [](const Level& tile_level, int x, int y) {
            return x >= 0 && y >= 0 && x < tile_level.ds1.width() && y < tile_level.ds1.height();
        };
        if (inside(level, cell_x, cell_y)) return { &level, std::size_t(cell_y) * std::size_t(map_width) + std::size_t(cell_x) };
        for (const auto& neighbour : level.nearby)
            if (inside(*neighbour.level, cell_x - neighbour.dx, cell_y - neighbour.dy))
                return { neighbour.level, std::size_t(cell_y - neighbour.dy) * std::size_t(neighbour.level->ds1.width()) + std::size_t(cell_x - neighbour.dx) };
        return { nullptr, 0 };
    };

    const auto& upal_splash = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    // Lower walls (16..19: cliff faces hanging below their cell) go first,
    // under every floor (FUN_004df480 before the floors' FUN_004ded10).
    for (int dy = -kReach; dy <= kReach; ++dy)
        for (int dx = -kReach; dx <= kReach; ++dx) {
            const int cell_x = base_x + dx, cell_y = base_y + dy;
            const auto [cell_level, off] = cell_at(cell_x, cell_y);
            if (!cell_level) continue;
            if (!cell_level->picks.empty()) {
                for (const auto& pick : cell_level->shown()[off])
                    if (pick.layer == 0 && pick.orient >= 16 && !pick.hidden) blit_cell(cell_x, cell_y, *pick.tile, 0, &pick);
                continue;
            }
            for (const auto& wall_layer : cell_level->ds1.walls()) {
                const auto& cell = wall_layer.cells[off];
                if (cell.hidden || cell.wall_type < 16) continue;
                if (auto* tile = find_tile(*cell_level, cell.style, cell.sequence, cell.wall_type)) blit_cell(cell_x, cell_y, *tile, 0, &cell);
            }
        }
    // Row-major sweep so back rows render first. dy increases downward
    // in screen space, so we iterate low→high dy for back-to-front.
    if (rain) {
        rain->floors(level.mud);                         // FUN_004de730: this pass's splash / bubble gates
        if (!level.rain) rain->splash_pass = false;      // outside a rain level the volume is 0 (FUN_00473f50 zeroes 0x7a89a0): no splash pass
    }
    for (int dy = -kReach; dy <= kReach; ++dy) {
        for (int dx = -kReach; dx <= kReach; ++dx) {
            const int cell_x = base_x + dx;
            const int cell_y = base_y + dy;
            const auto [cell_level, off] = cell_at(cell_x, cell_y);
            if (!cell_level) continue;
            const auto& cell_map = cell_level->ds1;
            // A floor drawn whose DT1 material flags have 2 may splash in
            // the rain or bubble in the mud (FUN_004de410).
            auto splash = [&](const d2d::dt1::Tile& tile, bool drawn) {
                if (rain && drawn && (tile.material_flags & 2)) { const auto [splash_x, splash_y] = iso(cell_x, cell_y); rain->floor(rain->rng, splash_x, splash_y); }
            };
            if (!cell_level->picks.empty()) {                   // the floors game.exe picked
                for (const auto& pick : cell_level->shown()[off])
                    if (pick.layer == 1 && !pick.hidden) splash(*pick.tile, blit_cell(cell_x, cell_y, *pick.tile, 1, &pick));
                continue;
            }

            // Floor (single layer typical). Type 0 in the floor stream
            // is the "no floor here" marker (dropped by the game); we
            // still need to look up type=0 for actual floors from DT1s.
            for (const auto& floor_layer : cell_map.floors()) {
                const auto& cell = floor_layer.cells[off];
                // A floor is there when prop1 bit 2 says so (FUN_0066e9b0);
                // (0, 0, 0) with it is the grass tile, without it nothing.
                if (cell.hidden || !(cell.prop1 & 2)) continue;
                if (auto* tile = find_tile(*cell_level, cell.style, cell.sequence, /*type=*/0)) {
                    splash(*tile, blit_cell(cell_x, cell_y, *tile, 1, &cell));
                }
            }
        }
    }

    // The rain's splashes and the mud's bubbles on the floor (FUN_00473c00 →
    // FUN_00473a70: draw mode 3, additive), each moved by the side panel's
    // quarter (-W/4 right open, +W/4 left) and drawn when that spot lies in
    // the half the panel leaves, above the bottom 47 pixels. game.exe bug
    // (bugs.md #38): the software modes don't shift the world, so they jump.
    if (rain) {
        const int shift = g_drawn.panel_side == 1 ? -(screen_w / 4) : g_drawn.panel_side == 2 ? screen_w / 4 : 0;
        const int left = g_drawn.panel_side == 2 ? screen_w / 4 * 2 : 0, right = g_drawn.panel_side == 1 ? screen_w - screen_w / 4 * 2 : screen_w;
        const auto overlay = [&](const std::array<d2d::dc6::Sprite, 4>& sprites, const d2d::rules::Rain::Splash& piece) {
            const int at_x = piece.x + shift;
            if (at_x < left || at_x >= right || piece.y < 0 || piece.y >= int(kScreenHeight) - 0x2f) return;
            if (const auto frames = sprites[std::size_t(piece.kind)].frames(); piece.frame < int(frames.size()))
                blit_additive(framebuffer, frames[std::size_t(piece.frame)], upal_splash, nullptr, at_x, piece.y);
        };
        for (const auto& rain_splash : rain->splashes) overlay(scene.rain_splash, rain_splash);
        for (const auto& bubble : rain->bubbles) overlay(scene.mud_bubble, bubble);
    }
    // Tile shadows (orientation 13), once every floor is down: a shadow
    // reaches over the cells after its own (FUN_004df510 → driver +0xa4,
    // after the floors' FUN_004ded10 and the splashes).
    for (int dy = -kReach; dy <= kReach; ++dy)
        for (int dx = -kReach; dx <= kReach; ++dx) {
            const int cell_x = base_x + dx, cell_y = base_y + dy;
            const auto [cell_level, off] = cell_at(cell_x, cell_y);
            if (!cell_level) continue;
            if (!cell_level->picks.empty()) {
                for (const auto& pick : cell_level->shown()[off])
                    if (pick.layer == 2 && !pick.hidden) blit_cell(cell_x, cell_y, *pick.tile, 2, &pick);
                continue;
            }
            for (const auto& shadow_layer : cell_level->ds1.shadows()) {
                const auto& cell = shadow_layer.cells[off];
                if (cell.hidden) continue;
                if (cell.style == 0 && cell.sequence == 0 && cell.wall_type == 0) continue;
                if (auto* tile = find_tile(*cell_level, cell.style, cell.sequence, /*type=*/13))
                    blit_cell(cell_x, cell_y, *tile, 2, &cell);
            }
        }
    // Units' shadows, on the ground under the walls and units (the floor
    // pass FUN_004df510 → FUN_004dc7b0).
    {
        static std::vector<std::uint16_t> mask;
        if (mask.size() != std::size_t(screen_w) * kScreenHeight) mask.assign(std::size_t(screen_w) * kScreenHeight, 0);
        static std::uint16_t id = 0;
        for (const auto& unit : units) {
            if (!unit.anim || !unit.shadow) continue;
            const float back = unit.object ? 0.1f : 0.f;      // game.exe's position, as the unit's draw
            const auto [anchor_x, anchor_y] = iso_point(unit.x - back, unit.y - back);
            if (anchor_x < -200 || anchor_x > screen_w + 200 || anchor_y < -100 || anchor_y > int(kScreenHeight) + 300) continue;
            if (++id == 0) { std::ranges::fill(mask, std::uint16_t{ 0 }); id = 1; }
            auto elapsed = std::uint32_t(float(elapsed_ms - unit.mode_ms) * unit.rate);
            if (unit.hold && !unit.frames) elapsed = std::min(elapsed, std::uint32_t(std::max(int(unit.anim->cof.frames_per_direction()), 1) - 1) * unit.anim->ms_per_frame());
            elapsed = object_elapsed(unit, elapsed);
            if (past_cof(unit, elapsed)) continue;
            shadow_composite(framebuffer, *unit.anim, unit.dir, elapsed, anchor_x, anchor_y, mask, id);
        }
    }
    set_phase(MainPhase::IngameWalls);
    // Walls / trees / roofs — same row-major sweep, per-cell one-pass
    // draw. All non-floor orientation types share the same iso
    // positioning; the DT1 tile's own y_shift + per-block y encode the
    // vertical layout, so height-varying elements (columns, trees, roofs)
    // land correctly relative to the cell iso anchor without special
    // per-type math here. Roofs (type 15) get a small extra vertical
    // hoist from the DS1 orientation dword's upper 24 bits when
    // present — for MVP we use the DT1's per-tile roof_height instead.
    //
    // Walls go back to front by iso depth (gx + gy, one diagonal at a
    // time), and each unit is drawn once its own cell's diagonal is done:
    // a tent north of the player stays behind them, one south of them
    // covers them. Units on the same diagonal go in screen-y order.
    // game.exe buckets tiles by cell too (FUN_004dd7c0: FUN_00643340 gives
    // the tile's cell of the screen grid, each cell a slot of lists).
    // ponytail: how it interleaves units with a cell's walls isn't traced.
    std::vector<const Unit*> order;
    for (const auto& unit : units) if (unit.anim || unit.sprite || unit.missile) order.push_back(&unit);
    auto diag_of = [&](const Unit* unit) {
        return (int(std::floor(unit->x)) - base_x) + (int(std::floor(unit->y)) - base_y);
    };
    std::ranges::sort(order, {}, [](const Unit* unit) { return unit->x + unit->y; });
    std::size_t next_unit = 0;
    const auto& upal0 = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    auto draw_units_through = [&](int diag) {
        for (; next_unit < order.size() && diag_of(order[next_unit]) <= diag; ++next_unit) {
            const Unit& unit = *order[next_unit];
            // An object stands at game.exe's position, half a subtile before
            // its cell's (measured live: Lut Gholein's waypoint flames and braziers).
            const float back = unit.object ? 0.1f : 0.f;
            const auto [anchor_x, anchor_y] = iso_point(unit.x - back, unit.y - back);
            // Bright-alpha (draw mode 7): the unit under the cursor
            // (FUN_00467a10 -> FUN_00471ec0) and a Ghostly champion
            // (umod 36, FUN_005a1080). Its light doubles, clamped
            // 0x40..0xff.
            // ponytail: objects also switch to mode 7 under the cursor,
            // untraced.
            const bool bright = unit.highlight || unit.ghostly;
            const auto& lpal = !light ? upal0
                             : (*light->pal)[std::size_t((bright ? std::clamp(light->unit_at(unit.x, unit.y) * 2, 0x40, 0xff) : light->unit_at(unit.x, unit.y)) >> 3)];
            // A state's colour shift wins over the unit's own colour
            // (a monster's palshift / RandTransforms, an item's colormap).
            const auto* umap = unit.shift ? unit.shift : unit.cmap;
            const auto spal = umap ? Scene::mapped(lpal, umap) : d2d::palette::Palette{};
            const auto& upal = umap ? spal : lpal;
            // Its overlays: PreDraw ones behind it, the rest in front
            // (FUN_00470390: AnimRate x 16 / 256 frames a tick).
            auto draw_overs = [&](bool pre) {
                for (const auto& overlay : unit.overs) {
                    if (overlay.overlay->predraw != pre) continue;
                    const auto* spr = scene.overlay_sprite(*overlay.overlay);
                    if (!spr || elapsed_ms < overlay.start) continue;
                    const auto frames = std::uint32_t(std::min(overlay.overlay->frames, int(spr->frames_per_direction())));
                    const int frame_index = d2d::rules::overlay_frame(elapsed_ms - overlay.start, overlay.overlay->rate, int(frames), overlay.once);
                    if (frame_index < 0 || !spr->directions()) continue;
                    if (blit_dcc_frame(framebuffer, spr->frame(std::uint8_t(std::uint32_t(unit.dir) % spr->directions()), std::int32_t(frame_index)), upal0,
                                       anchor_x + overlay.overlay->x, anchor_y + overlay.overlay->dy(unit.overlay_class), draw_mode(overlay.overlay->trans)))
                        Scene::note_drawn("overlays", overlay.overlay->file);
                }
            };
            draw_overs(true);
            if (anchor_x < -200 || anchor_x > screen_w + 200 || anchor_y < -100 || anchor_y > int(kScreenHeight) + 300) continue;
            std::array<int, 4> bounds{};
            if (unit.missile) {
                const auto cel = scene.missile_cels.find(unit.missile->name);
                if (cel == scene.missile_cels.end()) continue;
                const auto& spr = cel->second;
                const std::uint32_t dirs = spr.directions(), fpd = std::uint32_t(spr.frames_per_direction());
                if (dirs == 0 || fpd == 0) continue;
                const auto frame = (elapsed_ms - unit.mode_ms) * std::uint32_t(unit.missile->anim_speed) / (40u * 16u)
                                   % std::min<std::uint32_t>(std::uint32_t(unit.missile->anim_len), fpd);
                if (blit_dcc_frame(framebuffer, spr.frame(d2d::game::missile_direction(unit.dir, int(dirs)), std::uint8_t(frame)), unit.missile->trans ? upal0 : upal, anchor_x, anchor_y, unit.missile->trans))
                    Scene::note_drawn("missiles", unit.missile->cel_file);
                continue;
            }
            if (unit.sprite) {
                const auto* flippy = flippy_frame(*unit.sprite, elapsed_ms - unit.mode_ms);
                if (!flippy) continue;
                blit_at_anchor(framebuffer, *flippy, upal, anchor_x, anchor_y);
                bounds = { anchor_x + flippy->offset_x, anchor_y + flippy->offset_y - int(flippy->height) + 1,
                      anchor_x + flippy->offset_x + int(flippy->width), anchor_y + flippy->offset_y + 1 };
                if (bounds[2] > 0 && bounds[0] < screen_w && bounds[3] > 0 && bounds[1] < int(kScreenHeight)) Scene::note_drawn("items", unit.sprite_file);   // on screen
                if (items) items->push_back({ &unit, bounds });
            } else {
                auto elapsed = std::uint32_t(float(elapsed_ms - unit.mode_ms) * unit.rate);
                const auto last = std::uint32_t(std::max(int(unit.anim->cof.frames_per_direction()), 1) - 1) * unit.anim->ms_per_frame();
                if (unit.hold && !unit.frames) elapsed = std::min(elapsed, last);
                if (unit.reverse) elapsed = last - std::min(elapsed, last);
                elapsed = object_elapsed(unit, elapsed);
                const bool blank = past_cof(unit, elapsed);
                if (!blank && unit.spirit) draw_composite(framebuffer, *unit.anim, tinted(upal, 205, 230, 255), unit.dir, elapsed, anchor_x, anchor_y, 4);   // half, pale
                else if (!blank) draw_composite(framebuffer, *unit.anim, upal, unit.dir, elapsed, anchor_x, anchor_y);
                if (!blank && hovered && unit.name && !unit.name->empty()) bounds = composite_bounds(*unit.anim, unit.dir, elapsed, anchor_x, anchor_y);
            }
            draw_overs(false);
            // Its overlay (npcalert: Xoffset -5, Yoffset -7, the NPCs'
            // OverlayHeight row 0; Trans 3, draw mode 3 additive), 16
            // frames at AnimRate 9.
            // Kind 3: loops without LoopWaitTime (FUN_004b3380 / 00470c30).
            if (unit.overlay && unit.overlay->directions() && unit.overlay->frames_per_direction()) {
                const int frame = d2d::rules::overlay_frame(elapsed_ms - unit.overlay_start, 9, int(unit.overlay->frames_per_direction()));
                if (frame >= 0) blit_dcc_frame(framebuffer, unit.overlay->frame(0, std::uint8_t(frame)), upal0, anchor_x - 5, anchor_y - 7, 1);
            }
            // Last drawn unit under the cursor = the frontmost one.
            if (hovered && unit.name && !unit.name->empty()) {
                if (mouse_x >= bounds[0] && mouse_x < bounds[2] && mouse_y >= bounds[1] && mouse_y < bounds[3])
                    *hovered = { &unit, bounds };
                // An object's name doesn't follow its frames (a waypoint's
                // flames): FUN_00454f30 puts its box's bottom at the feet
                // + 8 + NameOffset + 2; the label draw takes 2 off box[1].
                if (hovered->first == &unit && unit.name_offset) hovered->second[1] = anchor_y + 8 + *unit.name_offset + 2 + 2;
            }
        }
    };
    struct Roof { int cell_x, cell_y; const d2d::dt1::Tile* tile; const void* source; };
    std::vector<Roof> roofs;
    for (int diag = -2 * kReach; diag <= 2 * kReach; ++diag) {
        draw_units_through(diag - 1);
        for (int dx = std::max(-kReach, diag - kReach); dx <= std::min(kReach, diag + kReach); ++dx) {
            const int dy = diag - dx;
            const int cell_x = base_x + dx;
            const int cell_y = base_y + dy;
            const auto [cell_level, off] = cell_at(cell_x, cell_y);
            if (!cell_level) continue;
            const auto& cell_map = cell_level->ds1;
            auto draw_wall = [&](int type, const d2d::dt1::Tile& tile, int wall_index, const void* source) {
                if (type != 15) { blit_cell(cell_x, cell_y, tile, 0, source, wall_alpha(cell_level, int(off), type, wall_index, cell_x, cell_y)); return; }
                roofs.push_back({ cell_x, cell_y, &tile, source });   // roofs go last, over every wall and unit (FUN_004dea70 after FUN_004df1c0)
            };
            if (!cell_level->picks.empty()) {
                int wall_index = 0;
                for (const auto& pick : cell_level->shown()[off])
                    if (pick.layer == 0 && pick.orient != 13 && pick.orient < 16 && !pick.hidden) draw_wall(pick.orient, *pick.tile, wall_index++, &pick);
                continue;
            }
            int wall_index = 0;
            for (const auto& wall_layer : cell_map.walls()) {
                const auto& cell = wall_layer.cells[off];
                ++wall_index;
                if (cell.hidden) continue;
                const int type = cell.wall_type;
                if (type == 0) continue;         // floor marker in wall stream
                if (type == 13 || type >= 16) continue;        // shadow, lower wall (drawn above)
                if (auto* tile = find_tile(*cell_level, cell.style, cell.sequence, type)) draw_wall(type, *tile, wall_index, &cell);
                // A corner (3) is two tiles: the second picked with orientation 4 (FUN_0066e9b0).
                if (type == 3) if (auto* tile = find_tile(*cell_level, cell.style, cell.sequence, 4)) draw_wall(4, *tile, wall_index, &cell);
            }
        }
    }
    draw_units_through(1 << 20);
    for (const auto& roof : roofs) {
        const auto& tile = *roof.tile;
        // Roof — hoist by the DT1's own roof_height.
        auto [iso_x, iso_y] = iso(roof.cell_x, roof.cell_y);
        iso_y -= tile.roof_height;
        // A raised roof takes the ambient alone (FUN_004dea70 hands its
        // roof_height to FUN_004ddef0, which then fills the table with
        // the environment's light, no lamps); a flat one is lit as a floor.
        const Hole* cutout = g_roof_cutout ? &hole : nullptr;
        if (floor_drawn(iso_x - kIsoW / 2, iso_y - (std::abs(tile.height) - kIsoH))) g_drawn.cells.push_back({ roof.cell_x, roof.cell_y, roof.source });
        if (light && tile.roof_height) blit_dt1_tile(framebuffer, tile, (*light->pal)[std::size_t(std::clamp(light->ambient, 0, 255) >> 3)], iso_x - kIsoW / 2, iso_y - (std::abs(tile.height) - kIsoH), 255, cutout);
        else if (light) blit_dt1_tile_lit(framebuffer, tile, *light, iso_x - kIsoW / 2, iso_y - (std::abs(tile.height) - kIsoH), roof.cell_x, roof.cell_y, 255, cutout);
        else blit_dt1_tile(framebuffer, tile, pal, iso_x - kIsoW / 2, iso_y - (std::abs(tile.height) - kIsoH), 255, cutout);
    }
}

bool blit_dcc_frame(std::vector<std::uint8_t>& framebuffer,
                    const d2d::dcc::Frame& frame,
                    const d2d::palette::Palette& pal,
                    int anchor_x, int anchor_y, int trans) {
    bool wrote = false;
    const int dst_x = anchor_x + frame.box_left;
    const int dst_y = anchor_y + frame.box_top;
    const int screen_w = fb_width(framebuffer);
    for (std::int32_t y = 0; y < frame.height; ++y) {
        const int pixel_y = dst_y + y;
        if (pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
        const auto* row = frame.pixels.data() + std::size_t(y) * frame.width;
        for (std::int32_t x = 0; x < frame.width; ++x) {
            const auto idx = row[x];
            if (idx == 0) continue;
            const int pixel_x = dst_x + x;
            if (pixel_x < 0 || pixel_x >= screen_w) continue;
            const auto colour = pal[idx];
            auto* pixel = framebuffer.data() + (std::size_t(pixel_y) * std::size_t(screen_w) + std::size_t(pixel_x)) * 4;
            if (trans == 1) { pixel[0] = std::uint8_t(std::min(255, pixel[0] + colour.r)); pixel[1] = std::uint8_t(std::min(255, pixel[1] + colour.g)); pixel[2] = std::uint8_t(std::min(255, pixel[2] + colour.b)); }
            else if (trans == 2) { pixel[0] = std::uint8_t(pixel[0] * colour.r / 255); pixel[1] = std::uint8_t(pixel[1] * colour.g / 255); pixel[2] = std::uint8_t(pixel[2] * colour.b / 255); }
            else if (trans >= 3 && trans <= 5) {                       // a quarter, half, three quarters of the layer
                const int mode = trans - 2;
                pixel[0] = std::uint8_t((pixel[0] * (4 - mode) + colour.r * mode) / 4); pixel[1] = std::uint8_t((pixel[1] * (4 - mode) + colour.g * mode) / 4); pixel[2] = std::uint8_t((pixel[2] * (4 - mode) + colour.b * mode) / 4);
            }
            else { pixel[0] = colour.r; pixel[1] = colour.g; pixel[2] = colour.b; }
            pixel[3] = 0xFF;
            wrote = true;
        }
    }
    return wrote;
}

void shadow_composite(std::vector<std::uint8_t>& framebuffer, const Scene::PlayerAnim& anim, int dir_want, std::uint32_t elapsed_ms,
                      int anchor_x, int anchor_y, std::vector<std::uint16_t>& mask, std::uint16_t id) {
    composite_frames(anim, dir_want, elapsed_ms, [&](const d2d::dcc::Frame& frame, const d2d::cof::Layer* layer) {
        const int screen_w = fb_width(framebuffer);
        if (layer && !layer->shadow) return;                               // the COF says this layer casts none
        const int bottom = frame.box_top + frame.height - 1;              // the frame's bottom row, from the anchor
        const int left = anchor_x + frame.box_left + bottom / 2, top = anchor_y + bottom / 2;
        for (std::int32_t pixel_row = 0; pixel_row < frame.height; pixel_row += 2) {          // rows up from the bottom
            const int pixel_y = top - pixel_row / 2;
            if (pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
            const auto* row = frame.pixels.data() + std::size_t(frame.height - 1 - pixel_row) * frame.width;
            for (std::int32_t x = 0; x < frame.width; ++x) {
                const int pixel_x = left + x - pixel_row / 2;
                if (row[x] == 0 || pixel_x < 0 || pixel_x >= screen_w) continue;
                auto& mask_cell = mask[std::size_t(pixel_y) * std::size_t(screen_w) + std::size_t(pixel_x)];
                if (mask_cell == id) continue;
                mask_cell = id;
                auto* pixel = framebuffer.data() + (std::size_t(pixel_y) * std::size_t(screen_w) + std::size_t(pixel_x)) * 4;
                pixel[0] = std::uint8_t(pixel[0] / 4); pixel[1] = std::uint8_t(pixel[1] / 4); pixel[2] = std::uint8_t(pixel[2] / 4);
            }
        }
    });
}

void draw_composite(std::vector<std::uint8_t>& framebuffer, const Scene::PlayerAnim& anim,
                    const d2d::palette::Palette& pal, int dir_want,
                    std::uint32_t elapsed_ms, int anchor_x, int anchor_y, int trans) {
    bool blitted = false;
    composite_frames(anim, dir_want, elapsed_ms, [&](const d2d::dcc::Frame& frame, const d2d::cof::Layer* layer) {
        blitted |= blit_dcc_frame(framebuffer, frame, pal, anchor_x, anchor_y, trans >= 0 ? trans : layer_trans(layer));
    });
    // D2D_ANIM_LOG counts what reached the screen, not what loaded (the Tree of Inifuss loaded and drew nothing).
    if (blitted && !anim.noted && !anim.kind.empty()) { anim.noted = true; Scene::note_drawn(anim.kind, anim.name); }
}

}  // namespace d2d::client
