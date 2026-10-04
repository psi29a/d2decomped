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

void blit_dt1_tile(std::vector<std::uint8_t>& framebuffer,
                   const d2d::dt1::Tile& tile,
                   const d2d::palette::Palette& pal,
                   int screen_x, int screen_y, int alpha_all , const Hole* hole) {
    const int tile_height = std::abs(tile.height);
    for (int y = 0; y < tile_height; ++y) {
        const int pixel_y = screen_y + y;
        if (pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
        const auto* row = tile.pixels.data() + std::size_t(y) * tile.width;
        for (int x = 0; x < tile.width; ++x) {
            const std::uint8_t idx = row[x];
            if (idx == 0) continue;   // transparent
            const int pixel_x = screen_x + x;
            if (pixel_x < 0 || pixel_x >= int(kScreenWidth)) continue;
            const auto colour = pal[idx];
            auto* pixel = framebuffer.data() + (std::size_t(pixel_y) * kScreenWidth + pixel_x) * 4;
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
    for (int y = 0; y < tile_height; ++y) {
        const int pixel_y = screen_y + y;
        if (pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
        const auto* row = tile.pixels.data() + std::size_t(y) * tile.width;
        for (int x = 0; x < tile.width; ++x) {
            const std::uint8_t idx = row[x];
            const int pixel_x = screen_x + x;
            if (idx == 0 || pixel_x < 0 || pixel_x >= int(kScreenWidth)) continue;
            const auto colour = pal[idx];
            auto* pixel = framebuffer.data() + (std::size_t(pixel_y) * kScreenWidth + pixel_x) * 4;
            pixel[0] = std::uint8_t((pixel[0] * 3 + colour.r) / 4); pixel[1] = std::uint8_t((pixel[1] * 3 + colour.g) / 4); pixel[2] = std::uint8_t((pixel[2] * 3 + colour.b) / 4);
        }
    }
}

void blit_dt1_tile_lit(std::vector<std::uint8_t>& framebuffer, const d2d::dt1::Tile& tile, const Lighting& light,
                       int screen_x, int screen_y, int cell_x, int cell_y, int top_x, int top_y, bool floor, int alpha_all , const Hole* hole) {
    // The subtile corners round the cell: a tile's pixels reach up to 6
    // subtiles before its top corner (tall floors) and 8 past.
    constexpr int kPatch = 16, kOrigin = 6;
    std::array<float, kPatch * kPatch> corners{};
    int low = 255, high = 0;
    for (int j = 0; j < kPatch; ++j)
        for (int i = 0; i < kPatch; ++i) {
            const int value = light.grid.at(cell_x * 5 - kOrigin + i, cell_y * 5 - kOrigin + j);
            corners[std::size_t(j * kPatch + i)] = float(value);
            low = std::min(low, value); high = std::max(high, value);
        }
    const auto& pals = *light.pal;
    if (low >> 3 == high >> 3) { blit_dt1_tile(framebuffer, tile, pals[std::size_t(low >> 3)], screen_x, screen_y, alpha_all, hole); return; }
    // Screen offset from the top corner → subtiles into the cell: dx - dy =
    // x / 80 cells, dx + dy = y / 40.
    auto level = [&](int offset_x, int offset_y) {
        const float along_x = float(offset_x) / (kIsoW / 2), along_y = float(offset_y) / (kIsoH / 2);
        const float grid_a = std::clamp((along_y + along_x) * 2.5f + kOrigin, 0.f, kPatch - 1.001f), grid_b = std::clamp((along_y - along_x) * 2.5f + kOrigin, 0.f, kPatch - 1.001f);
        const int column = int(grid_a), row = int(grid_b);
        const float frac_a = grid_a - float(column), fb2 = grid_b - float(row);
        const float* corner = &corners[std::size_t(row * kPatch + column)];
        return std::size_t(int((corner[0] + (corner[1] - corner[0]) * frac_a) * (1 - fb2) + (corner[kPatch] + (corner[kPatch + 1] - corner[kPatch]) * frac_a) * fb2) >> 3);
    };
    const int tile_height = std::abs(tile.height);
    for (int y = 0; y < tile_height; ++y) {
        const int pixel_y = screen_y + y;
        if (pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
        const auto* row = tile.pixels.data() + std::size_t(y) * tile.width;
        for (int x = 0; x < tile.width; ++x) {
            const std::uint8_t idx = row[x];
            if (idx == 0) continue;
            const int pixel_x = screen_x + x;
            if (pixel_x < 0 || pixel_x >= int(kScreenWidth)) continue;
            const auto lvl = level(pixel_x - top_x, floor ? pixel_y - top_y : kIsoH / 2);
            const auto col = pals[lvl][idx];
            auto* pixel = framebuffer.data() + (std::size_t(pixel_y) * kScreenWidth + pixel_x) * 4;
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
        if (dir >= spr.directions() || frame >= spr.frames_per_direction()) continue;
        const auto& frame_ref = spr.frame(dir, frame);
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
    const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
    const int cx0 = int(kScreenWidth) / 2;
    const int cy0 = int(kScreenHeight) / 2;
    const int map_width  = map.width();
    const int base_x = int(std::floor(cam_x)), base_y = int(std::floor(cam_y));
    // Screen position of cell (gx, gy)'s top diamond corner. The camera
    // point (cam_x, cam_y) — continuous, in cells — lands at (kW/2,
    // kH/2 + kIsoH/2), i.e. a cell centre when the camera sits on one.
    auto iso = [&](int cell_x, int cell_y) {
        const float dx = float(cell_x) - cam_x, dy = float(cell_y) - cam_y;
        return std::pair{ cx0 + int(std::lround((dx - dy) * (kIsoW / 2))),
                          cy0 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2))) };
    };
    // Screen position of a continuous world point (a unit's feet).
    auto iso_point = [&](float x, float y) {
        const float dx = x - cam_x, dy = y - cam_y;
        return std::pair{ cx0 + int(std::lround((dx - dy) * (kIsoW / 2))),
                          cy0 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2))) };
    };

    // Iso footprint for the 800x600 window: each screen cell is 160x80.
    // A ±12 grid-cell window around the camera covers > 2× screen area,
    // leaving room for tall walls (up to 128+px) to reach in from cells
    // that are off-screen at their base.
    constexpr int kReach = 12;

    // Blit a single tile at cell (gx, gy)'s iso position, honouring the
    // 80-tall-diamond-at-bottom convention shared by floor/wall pixel
    // buffers.
    // `layer`: 0 wall, 1 floor, 2 shadow (the pass, not the DT1's own type field).
    auto blit_cell = [&](int cell_x, int cell_y, const d2d::dt1::Tile& tile, int layer, int alpha = 255) {
        const auto [iso_x, iso_y] = iso(cell_x, cell_y);
        const int tile_height = std::abs(tile.height);
        const int screen_x = iso_x - kIsoW / 2;         // block x counts from the cell's left corner, whatever the tile's width
        const int screen_y = iso_y - (tile_height - kIsoH);
        if (layer == 2) blit_dt1_shadow(framebuffer, tile, pal, screen_x, screen_y);
        else if (light) blit_dt1_tile_lit(framebuffer, tile, *light, screen_x, screen_y, cell_x, cell_y, iso_x, iso_y, layer == 1, alpha);
        else blit_dt1_tile(framebuffer, tile, pal, screen_x, screen_y, alpha);
    };
    // Walls in front of the player see-through (FUN_004dd060 /
    // FUN_004dd180): a wall 1..3 cells past the player's cell in x
    // (orientations 1 4 5 7 8 10 12) or in y (2 3 6 7 9 11 12) fades to
    // alpha 0x80 over 500 ms, and back to 0xff once it isn't. Roofs (15)
    // and lower walls (16..19) never fade.
    // ponytail: the blend is linear in RGB, not the driver's alpha table;
    // game.exe's room-based mode (DAT_0072a968) isn't built.
    struct Fade { int from = 255, target = 255; std::uint32_t changed_at = 0; };
    static std::unordered_map<std::uint64_t, Fade> fades;
    const int pcx = int(std::floor(cam_x)), pcy = int(std::floor(cam_y));
    const auto [hole_x, hole_y] = iso_point(cam_x, cam_y);
    const Hole hole{ hole_x, hole_y - 40, 70 };           // round the player's body
    auto wall_alpha = [&](const void* level_key, int off, int type, int wall_index, int cell_x, int cell_y) {
        static constexpr std::uint16_t kFadeOnX = 1 << 1 | 1 << 4 | 1 << 5 | 1 << 7 | 1 << 8 | 1 << 10 | 1 << 12;
        static constexpr std::uint16_t kFadeOnY = 1 << 2 | 1 << 3 | 1 << 6 | 1 << 7 | 1 << 9 | 1 << 11 | 1 << 12;
        const bool see = type < 16 && ((cell_x > pcx && cell_x < pcx + 4 && (kFadeOnX >> type & 1)) || (cell_y > pcy && cell_y < pcy + 4 && (kFadeOnY >> type & 1)));
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

    const auto& upal_splash = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
    // Row-major sweep so back rows render first. dy increases downward
    // in screen space, so we iterate low→high dy for back-to-front.
    for (int dy = -kReach; dy <= kReach; ++dy) {
        for (int dx = -kReach; dx <= kReach; ++dx) {
            const int cell_x = base_x + dx;
            const int cell_y = base_y + dy;
            const auto [cell_level, off] = cell_at(cell_x, cell_y);
            if (!cell_level) continue;
            const auto& cell_map = cell_level->ds1;
            // A floor whose DT1 material flags have 2 may splash in the rain
            // (FUN_004de410, as the tile's drawn).
            auto splash = [&](const d2d::dt1::Tile& tile) {
                if (rain && (tile.material_flags & 2)) { const auto [splash_x, splash_y] = iso(cell_x, cell_y); rain->floor(rain->rng, splash_x, splash_y); }
            };
            if (!cell_level->picks.empty()) {                   // the tiles game.exe picked: floors, then shadows
                for (const int layer : { 1, 2 })
                    for (const auto& pick : cell_level->picks[off])
                        if (pick.layer == layer && !pick.hidden) { blit_cell(cell_x, cell_y, *pick.tile, layer); if (layer == 1) splash(*pick.tile); }
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
                    blit_cell(cell_x, cell_y, *tile, 1);
                    splash(*tile);
                }
            }

            // Shadow layer: blended over the floor (blit_dt1_shadow).
            for (const auto& shadow_layer : cell_map.shadows()) {
                const auto& cell = shadow_layer.cells[off];
                if (cell.hidden) continue;
                if (cell.style == 0 && cell.sequence == 0 && cell.wall_type == 0) continue;
                if (auto* tile = find_tile(*cell_level, cell.style, cell.sequence, /*type=*/13))
                    blit_cell(cell_x, cell_y, *tile, 2);
            }
        }
    }

    // The rain's splashes on the floor (FUN_00473c00 → FUN_00473a70: draw
    // mode 3, additive).
    if (rain)
        for (const auto& rain_splash : rain->splashes)
            if (const auto frames = scene.rain_splash[std::size_t(rain_splash.kind)].frames(); rain_splash.frame < int(frames.size()))
                blit_additive(framebuffer, frames[std::size_t(rain_splash.frame)], upal_splash, nullptr, rain_splash.x, rain_splash.y);
    // Units' shadows, on the ground under the walls and units (the floor
    // pass FUN_004df510 → FUN_004dc7b0).
    {
        static std::vector<std::uint16_t> mask(std::size_t(kScreenWidth) * kScreenHeight, 0);
        static std::uint16_t id = 0;
        for (const auto& unit : units) {
            if (!unit.anim || !unit.shadow) continue;
            const auto [anchor_x, anchor_y] = iso_point(unit.x, unit.y);
            if (anchor_x < -200 || anchor_x > int(kScreenWidth) + 200 || anchor_y < -100 || anchor_y > int(kScreenHeight) + 300) continue;
            if (++id == 0) { std::ranges::fill(mask, std::uint16_t{ 0 }); id = 1; }
            shadow_composite(framebuffer, *unit.anim, unit.dir, std::uint32_t(float(elapsed_ms - unit.mode_ms) * unit.rate), anchor_x, anchor_y, mask, id);
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
    const auto& upal0 = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
    auto draw_units_through = [&](int diag) {
        for (; next_unit < order.size() && diag_of(order[next_unit]) <= diag; ++next_unit) {
            const Unit& unit = *order[next_unit];
            const auto [anchor_x, anchor_y] = iso_point(unit.x, unit.y);
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
                    auto frame_index = (elapsed_ms - overlay.start) * std::uint32_t(std::max(overlay.overlay->rate, 1)) / 640;
                    if (overlay.once && frame_index >= frames) continue;
                    frame_index %= frames;
                    blit_dcc_frame(framebuffer, spr->frame(std::uint8_t(std::uint32_t(unit.dir) % spr->directions()), std::int32_t(frame_index)), upal0,
                                   anchor_x + overlay.overlay->x, anchor_y + overlay.overlay->dy(unit.overlay_class), draw_mode(overlay.overlay->trans));
                }
            };
            draw_overs(true);
            if (anchor_x < -200 || anchor_x > int(kScreenWidth) + 200 || anchor_y < -100 || anchor_y > int(kScreenHeight) + 300) continue;
            std::array<int, 4> bounds{};
            if (unit.missile) {
                const auto cel = scene.missile_cels.find(unit.missile->name);
                if (cel == scene.missile_cels.end()) continue;
                const auto& spr = cel->second;
                const std::uint32_t dirs = spr.directions(), fpd = std::uint32_t(spr.frames_per_direction());
                if (dirs == 0 || fpd == 0) continue;
                const auto frame = (elapsed_ms - unit.mode_ms) * std::uint32_t(unit.missile->anim_speed) / (40u * 16u)
                                   % std::min<std::uint32_t>(std::uint32_t(unit.missile->anim_len), fpd);
                blit_dcc_frame(framebuffer, spr.frame(std::uint8_t(std::uint32_t(unit.dir) % dirs), std::uint8_t(frame)), unit.missile->trans ? upal0 : upal, anchor_x, anchor_y, unit.missile->trans);
                continue;
            }
            if (unit.sprite) {
                const auto* flippy = flippy_frame(*unit.sprite, elapsed_ms - unit.mode_ms);
                if (!flippy) continue;
                blit_at_anchor(framebuffer, *flippy, upal, anchor_x, anchor_y);
                bounds = { anchor_x + flippy->offset_x, anchor_y + flippy->offset_y - int(flippy->height) + 1,
                      anchor_x + flippy->offset_x + int(flippy->width), anchor_y + flippy->offset_y + 1 };
                if (items) items->push_back({ &unit, bounds });
            } else {
                auto elapsed = std::uint32_t(float(elapsed_ms - unit.mode_ms) * unit.rate);
                if (unit.hold) elapsed = std::min(elapsed, std::uint32_t(std::max(int(unit.anim->cof.frames_per_direction()), 1) - 1) * unit.anim->ms_per_frame());
                draw_composite(framebuffer, *unit.anim, upal, unit.dir, elapsed, anchor_x, anchor_y);
                if (hovered && unit.name && !unit.name->empty()) bounds = composite_bounds(*unit.anim, unit.dir, elapsed, anchor_x, anchor_y);
            }
            draw_overs(false);
            // Its overlay (npcalert: Xoffset -5, Yoffset -7, the NPCs'
            // OverlayHeight row 0; Trans 3, draw mode 3 additive), 16
            // frames at AnimRate 9.
            // ponytail: AnimRate read as frames a second; LoopWaitTime
            // (7000) not applied.
            if (unit.overlay && unit.overlay->directions() && unit.overlay->frames_per_direction())
                blit_dcc_frame(framebuffer, unit.overlay->frame(0, std::uint8_t(elapsed_ms / 111 % unit.overlay->frames_per_direction())), upal0, anchor_x - 5, anchor_y - 7, 1);
            // Last drawn unit under the cursor = the frontmost one.
            if (hovered && unit.name && !unit.name->empty()) {
                if (mouse_x >= bounds[0] && mouse_x < bounds[2] && mouse_y >= bounds[1] && mouse_y < bounds[3])
                    *hovered = { &unit, bounds };
            }
        }
    };
    for (int diag = -2 * kReach; diag <= 2 * kReach; ++diag) {
        draw_units_through(diag - 1);
        for (int dx = std::max(-kReach, diag - kReach); dx <= std::min(kReach, diag + kReach); ++dx) {
            const int dy = diag - dx;
            const int cell_x = base_x + dx;
            const int cell_y = base_y + dy;
            const auto [cell_level, off] = cell_at(cell_x, cell_y);
            if (!cell_level) continue;
            const auto& cell_map = cell_level->ds1;
            auto draw_wall = [&](int type, const d2d::dt1::Tile& tile, int wall_index) {
                if (type != 15) { blit_cell(cell_x, cell_y, tile, 0, wall_alpha(cell_level, int(off), type, wall_index, cell_x, cell_y)); return; }
                // Roof — hoist by the DT1's own roof_height.
                auto [iso_x, iso_y] = iso(cell_x, cell_y);
                iso_y -= tile.roof_height;
                // A roof is flat: lit like a floor, where each pixel lies on
                // the roof's plane (its top corner at the hoisted iso_y).
                const Hole* cutout = g_roof_cutout ? &hole : nullptr;
                if (light) blit_dt1_tile_lit(framebuffer, tile, *light, iso_x - kIsoW / 2, iso_y - (std::abs(tile.height) - kIsoH), cell_x, cell_y, iso_x, iso_y, true, 255, cutout);
                else blit_dt1_tile(framebuffer, tile, pal, iso_x - kIsoW / 2, iso_y - (std::abs(tile.height) - kIsoH), 255, cutout);
            };
            if (!cell_level->picks.empty()) {
                int wall_index = 0;
                for (const auto& pick : cell_level->picks[off])
                    if (pick.layer == 0 && pick.orient != 13 && !pick.hidden) draw_wall(pick.orient, *pick.tile, wall_index++);
                continue;
            }
            int wall_index = 0;
            for (const auto& wall_layer : cell_map.walls()) {
                const auto& cell = wall_layer.cells[off];
                ++wall_index;
                if (cell.hidden) continue;
                const int type = cell.wall_type;
                if (type == 0) continue;         // floor marker in wall stream
                if (type == 13) continue;        // shadow (drawn above)
                if (auto* tile = find_tile(*cell_level, cell.style, cell.sequence, type)) draw_wall(type, *tile, wall_index);
                // A corner (3) is two tiles: the second picked with orientation 4 (FUN_0066e9b0).
                if (type == 3) if (auto* tile = find_tile(*cell_level, cell.style, cell.sequence, 4)) draw_wall(4, *tile, wall_index);
            }
        }
    }
    draw_units_through(1 << 20);
}

void blit_dcc_frame(std::vector<std::uint8_t>& framebuffer,
                    const d2d::dcc::Frame& frame,
                    const d2d::palette::Palette& pal,
                    int anchor_x, int anchor_y, int trans) {
    const int dst_x = anchor_x + frame.box_left;
    const int dst_y = anchor_y + frame.box_top;
    for (std::int32_t y = 0; y < frame.height; ++y) {
        const int pixel_y = dst_y + y;
        if (pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
        const auto* row = frame.pixels.data() + std::size_t(y) * frame.width;
        for (std::int32_t x = 0; x < frame.width; ++x) {
            const auto idx = row[x];
            if (idx == 0) continue;
            const int pixel_x = dst_x + x;
            if (pixel_x < 0 || pixel_x >= int(kScreenWidth)) continue;
            const auto colour = pal[idx];
            auto* pixel = framebuffer.data() + (std::size_t(pixel_y) * kScreenWidth + pixel_x) * 4;
            if (trans == 1) { pixel[0] = std::uint8_t(std::min(255, pixel[0] + colour.r)); pixel[1] = std::uint8_t(std::min(255, pixel[1] + colour.g)); pixel[2] = std::uint8_t(std::min(255, pixel[2] + colour.b)); }
            else if (trans == 2) { pixel[0] = std::uint8_t(pixel[0] * colour.r / 255); pixel[1] = std::uint8_t(pixel[1] * colour.g / 255); pixel[2] = std::uint8_t(pixel[2] * colour.b / 255); }
            else if (trans >= 3 && trans <= 5) {                       // a quarter, half, three quarters of the layer
                const int mode = trans - 2;
                pixel[0] = std::uint8_t((pixel[0] * (4 - mode) + colour.r * mode) / 4); pixel[1] = std::uint8_t((pixel[1] * (4 - mode) + colour.g * mode) / 4); pixel[2] = std::uint8_t((pixel[2] * (4 - mode) + colour.b * mode) / 4);
            }
            else { pixel[0] = colour.r; pixel[1] = colour.g; pixel[2] = colour.b; }
            pixel[3] = 0xFF;
        }
    }
}

void shadow_composite(std::vector<std::uint8_t>& framebuffer, const Scene::PlayerAnim& anim, int dir_want, std::uint32_t elapsed_ms,
                      int anchor_x, int anchor_y, std::vector<std::uint16_t>& mask, std::uint16_t id) {
    composite_frames(anim, dir_want, elapsed_ms, [&](const d2d::dcc::Frame& frame, const d2d::cof::Layer* layer) {
        if (layer && !layer->shadow) return;                               // the COF says this layer casts none
        const int bottom = frame.box_top + frame.height - 1;              // the frame's bottom row, from the anchor
        const int left = anchor_x + frame.box_left + bottom / 2, top = anchor_y + bottom / 2;
        for (std::int32_t pixel_row = 0; pixel_row < frame.height; pixel_row += 2) {          // rows up from the bottom
            const int pixel_y = top - pixel_row / 2;
            if (pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
            const auto* row = frame.pixels.data() + std::size_t(frame.height - 1 - pixel_row) * frame.width;
            for (std::int32_t x = 0; x < frame.width; ++x) {
                const int pixel_x = left + x - pixel_row / 2;
                if (row[x] == 0 || pixel_x < 0 || pixel_x >= int(kScreenWidth)) continue;
                auto& mask_cell = mask[std::size_t(pixel_y) * kScreenWidth + std::size_t(pixel_x)];
                if (mask_cell == id) continue;
                mask_cell = id;
                auto* pixel = framebuffer.data() + (std::size_t(pixel_y) * kScreenWidth + std::size_t(pixel_x)) * 4;
                pixel[0] = std::uint8_t(pixel[0] / 4); pixel[1] = std::uint8_t(pixel[1] / 4); pixel[2] = std::uint8_t(pixel[2] / 4);
            }
        }
    });
}

void draw_composite(std::vector<std::uint8_t>& framebuffer, const Scene::PlayerAnim& anim,
                    const d2d::palette::Palette& pal, int dir_want,
                    std::uint32_t elapsed_ms, int anchor_x, int anchor_y) {
    composite_frames(anim, dir_want, elapsed_ms, [&](const d2d::dcc::Frame& frame, const d2d::cof::Layer* layer) {
        blit_dcc_frame(framebuffer, frame, pal, anchor_x, anchor_y, layer_trans(layer));
    });
}

}  // namespace d2d::client
