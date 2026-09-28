// World rendering: DT1 tiles, depth-sorted units, DCC composites.
#pragma once

#include "common.hpp"
#include "scene.hpp"

namespace d2d::client {

// Blit one DT1 tile's pre-decoded palette-indexed pixels through `pal`.
// The tile's pixel buffer is (tile.width x abs(tile.height)); index 0 is
// transparent. Screen position is the buffer's top-left; caller does the
// iso math to place it. Bounds-checked per-pixel — off-screen tiles are
// clipped rather than skipped so the compositor can walk the whole grid.
// d2d's roof cut-out (deviations.md): roofs see-through in a soft circle
// round the player, so a player under a roof stays in sight. game.exe
// draws roofs whole (walls.md). Off: --toggle trans_roof=off.
inline bool g_roof_cutout = true;
struct Hole {
    int x = 0, y = 0, radius = 0;                   // screen centre and radius, pixels
    // The alpha a pixel at (px, py) keeps: 0x40 in the inner half, rising to
    // 0xff at the rim.
    [[nodiscard]] int alpha(int pixel_x, int pixel_y, int base_alpha) const {
        const int dx = pixel_x - x, dy = pixel_y - y, distance_sq = dx * dx + dy * dy;
        if (distance_sq >= radius * radius) return base_alpha;
        const float fraction = std::clamp((std::sqrt(float(distance_sq)) - float(radius) / 2) / (float(radius) / 2), 0.f, 1.f);
        return std::min(base_alpha, 0x40 + int(float(0xff - 0x40) * fraction));
    }
};
void blit_dt1_tile(std::vector<std::uint8_t>& framebuffer,
                   const d2d::dt1::Tile& tile,
                   const d2d::palette::Palette& pal,
                   int screen_x, int screen_y, int alpha_all = 255, const Hole* hole = nullptr);

// A shadow tile (orientation 13), Blended Shadows on (the Video Options
// default): through the palette's alpha table 0 (PL2 +0x3500), a quarter
// of the tile over three quarters of the ground, unlit (driver +0xa4,
// callback FUN_004f82d0 at alpha 0xc0).
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

// A DT1 tile of cell (gx, gy), its top corner on screen at (top_x, top_y),
// lit: a floor pixel by where it lies on the ground (the inverse of the
// iso projection), a wall's column where it crosses the cell's middle,
// bilinear between the cell's 6 x 6 subtile corners. One level for the tile
// when they share it.
void blit_dt1_tile_lit(std::vector<std::uint8_t>& framebuffer, const d2d::dt1::Tile& tile, const Lighting& light,
                       int screen_x, int screen_y, int cell_x, int cell_y, int top_x, int top_y, bool floor, int alpha_all = 255, const Hole* hole = nullptr);

// Render the loaded DS1 onto the framebuffer around the camera point
// (cam_x, cam_y), in cells. Draws in D2's back-to-front Z order:
//   1. All floor tiles (type=0) in row order — the ground plane
//   2. All shadow tiles (type=13) in row order — soft dark decals
//   3. Walls / trees / roofs per row, top-back rows first, so
//      lower/nearer rows can occlude higher/farther ones
//
// Missing tile lookups are silent — a cell whose (style, seq, type)
// tuple isn't in any loaded DT1 leaves whatever's below it showing
// through, which is the same behaviour D2 itself has for stripped
// tilesets.
// Something drawn in the wall pass by depth: the player, an NPC.
struct Unit {
    float x = 0, y = 0;                  // world position, cells
    const Scene::PlayerAnim* anim = nullptr;
    int dir = 0;
    const std::string* name = nullptr;   // hover label, if selectable
    // When the unit's current mode started: animations run from frame 0
    // of each mode, like game.exe's mode start (FUN_005533d0 zeroes the
    // 8.8 frame counter at unit+0x30).
    std::uint32_t mode_ms = 0;
    int npc = -1;                        // Level::npcs index, -1 = the player
    // An item on the ground instead of a composite: its flippy DC6, played
    // once from mode_ms (a frame a tick), then held on the last frame.
    const d2d::dc6::Sprite* sprite = nullptr;
    const std::uint8_t* cmap = nullptr;  // ... through the item's colormap (Scene::item_map)
    std::array<std::uint8_t, 3> rgb{ 255, 255, 255 };   // hover label colour
    // A missile: its DCC in direction `dir` (0..31), looping AnimLen frames
    // at AnimSpeed/16 a tick from mode_ms.
    const Scene::MissileInfo* missile = nullptr;
    float rate = 1.f;                    // animation speed (attack speed, FHR, FBR)
    const d2d::dcc::Sprite* overlay = nullptr;   // over it (Overlay.txt npcalert: the quest balloon)
    // Its states' look (Scene::StateInfo): the colour shift (256, before
    // the light) and overlays, each from when it started; `once` plays a
    // cast overlay through a single time.
    const std::uint8_t* shift = nullptr;
    struct Over { const GameData::OverlayInfo* overlay = nullptr; std::uint32_t start = 0; bool once = false; };
    std::vector<Over> overs;
    bool highlight = false;              // under the cursor: drawn at twice its light (FUN_00471ec0)
    int overlay_class = 0;               // Overlay.txt's Height: FUN_006223a0 (players 1, monsters OverlayHeight - 1)
    bool shadow = true;                  // a composite casts one (players, monsters; MonStats2 Shadow), objects don't
};

// `trans`: a missile's Missiles.txt Trans, its draw mode (0 opaque).
void blit_dcc_frame(std::vector<std::uint8_t>& framebuffer, const d2d::dcc::Frame& frame,
                    const d2d::palette::Palette& pal, int anchor_x, int anchor_y, int trans = 0);

// The DC6 frame a ground item shows `elapsed` ms after it dropped.
void shadow_composite(std::vector<std::uint8_t>& framebuffer, const Scene::PlayerAnim& anim, int dir_want, std::uint32_t elapsed_ms,
                      int anchor_x, int anchor_y, std::vector<std::uint16_t>& mask, std::uint16_t id);

const d2d::dc6::Frame* flippy_frame(const d2d::dc6::Sprite& sprite, std::uint32_t elapsed);

// Screen rectangle a composite's current frame covers with its feet at
// (ax, ay): the union of every drawn layer's frame box. {x0, y0, x1, y1}.
std::array<int, 4> composite_bounds(const Scene::PlayerAnim& anim, int dir_want,
                                    std::uint32_t elapsed_ms, int anchor_x, int anchor_y);

inline int draw_mode(int effect);
void render_world(std::vector<std::uint8_t>& framebuffer,
                  const Scene& scene,
                  const Level& level,
                  float cam_x, float cam_y,
                  std::uint32_t elapsed_ms = 0,
                  std::span<const Unit> units = {},
                  int mouse_x = -1, int mouse_y = -1,
                  std::pair<const Unit*, std::array<int, 4>>* hovered = nullptr,
                  const Lighting* light = nullptr, d2d::rules::Rain* rain = nullptr,
                  std::vector<std::pair<const Unit*, std::array<int, 4>>>* items = nullptr);

// In-game placeholder — a hero has been created; we don't have the actual
// world/map render yet, so celebrate the character info and offer Esc to
// go back to the title. Using the credits bg (dark corridor) as backdrop.
// Blit a DCC frame with its origin at (anchor_x, anchor_y). A DCC frame's
// y_offset is its BOTTOM row relative to the origin (feet), so the pixel
// block's top-left is (box_left, box_top). Palette-indexed; index 0 is
// transparent so limbs compose cleanly over each other and over tiles.
// Trans 1 / 2 are draw modes 3 / 4, which the software renderer blends
// through the palette's PL2 tables (FUN_00511d70; D2WinPalette.cpp copies
// them from PL2 +0x33500 additive and +0x43500 multiply).
// ponytail: the same sums in RGB, not the tables' nearest palette colours.
void blit_dcc_frame(std::vector<std::uint8_t>& framebuffer,
                    const d2d::dcc::Frame& frame,
                    const d2d::palette::Palette& pal,
                    int anchor_x, int anchor_y, int trans);

// Draw a composite's current frame with its feet at (anchor_x, anchor_y):
// every loaded layer, in the COF's per-(direction, frame) draw order.
// Frame time from the COF speed byte: D2 advances speed/256 frames per
// 25 Hz tick, so one frame lasts 40 ms * 256 / speed (BA 80 -> 128 ms).
// ponytail: COF speed as the rate; AnimData.d2 is authoritative — read it
// when an animation visibly runs at the wrong pace. No shadow, no
// transparent-layer draw effects yet (no TN layer sets `transparent`).
// Each layer's frame of a composite at `elapsed_ms`, in the COF's
// per-(direction, frame) draw order.
// The layer's COF record (its draw effect, whether it casts a shadow).
inline const d2d::cof::Layer* cof_layer(const Scene::PlayerAnim& anim, std::uint8_t type) {
    for (const auto& layer : anim.cof.layer_defs()) if (layer.type == type) return &layer;
    return nullptr;
}
// A transparent COF layer's draw effect as blit_dcc_frame's mode: the
// driver's draw modes 0..2 (a quarter, half, three quarters of the layer),
// 3 additive, 4 multiply (like Missiles.txt Trans 1 / 2), else opaque.
// unverified (source: OpenDiablo2's naming): which alpha each of 0..2 is.
inline int draw_mode(int effect) {
    switch (effect) { case 0: return 3; case 1: return 4; case 2: return 5; case 3: return 1; case 4: return 2; default: return 0; }
}
inline int layer_trans(const d2d::cof::Layer* layer) { return layer && layer->transparent ? draw_mode(layer->draw_effect) : 0; }
template <class Fn> void composite_frames(const Scene::PlayerAnim& anim, int dir_want, std::uint32_t elapsed_ms, Fn&& visit) {
    const auto dirs = anim.cof.directions();
    const auto fpd  = anim.cof.frames_per_direction();
    if (dirs == 0 || fpd == 0) return;
    const auto dir = cof_direction(dir_want, dirs);
    // 25 ticks/s; each tick advances speed/256 frames.
    const auto ms_per_frame = anim.ms_per_frame();
    const auto frame = std::uint8_t((elapsed_ms / ms_per_frame) % fpd);
    for (const auto type : anim.cof.priority(d2d::cof::Cof::priority_row(dir, dirs), frame)) {
        if (type >= anim.dcc.size()) continue;
        const auto& spr = anim.layer(type);
        if (dir >= spr.directions() || frame >= spr.frames_per_direction()) continue;
        visit(spr.frame(dir, frame), cof_layer(anim, type));
    }
}

void draw_composite(std::vector<std::uint8_t>& framebuffer, const Scene::PlayerAnim& anim,
                    const d2d::palette::Palette& pal, int dir_want,
                    std::uint32_t elapsed_ms, int anchor_x, int anchor_y);

// A unit's shadow (driver +0x90, 0x5122e0 → FUN_00608d60): each frame
// from its bottom row up, every other row, a row up and a pixel left each
// time — half as tall, leaning up and left — its pixels darkening the
// ground through alpha table 0 as T[ground][0]: a quarter of the ground's
// colour (Blended Shadows on; off it's black). `mask` / `id`: a pixel
// darkens once however many layers cover it.
// ponytail: the darkening in RGB, not the table's palette colour.
void shadow_composite(std::vector<std::uint8_t>& framebuffer, const Scene::PlayerAnim& anim, int dir_want, std::uint32_t elapsed_ms,
                      int anchor_x, int anchor_y, std::vector<std::uint16_t>& mask, std::uint16_t id);

// Draws a composite frame, feet at the anchor.
void draw_composite(std::vector<std::uint8_t>& framebuffer, const Scene::PlayerAnim& anim,
                    const d2d::palette::Palette& pal, int dir_want,
                    std::uint32_t elapsed_ms, int anchor_x, int anchor_y);

}  // namespace d2d::client
