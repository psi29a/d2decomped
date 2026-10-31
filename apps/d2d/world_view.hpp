// SPDX-License-Identifier: GPL-3.0-or-later
// World rendering: DT1 tiles, depth-sorted units, DCC composites.
#pragma once

#include "common.hpp"
#include "scene.hpp"
#include "tile_blit.hpp"

#include <cof.hpp>
#include <dc6.hpp>
#include <dcc.hpp>
#include <dt1.hpp>
#include <light.hpp>
#include <palette.hpp>
#include <weather.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::client {

// d2d's roof cut-out (deviations.md): roofs see-through in a soft circle
// round the player, so a player under a roof stays in sight. game.exe
// draws roofs whole (walls.md). Off: --toggle trans_roof=off.
inline bool g_roof_cutout = true;
// --toggle light_grid: the light grid over the whole view (deviations.md
// #1); off, game.exe's 48 x 48 round the player (bugs.md #11).
inline bool g_light_grid = true;
inline bool g_autoparty = true;      // a joined game: invite the others, accept invites (deviations.md #7); off: --toggle autoparty=off
inline bool g_autoloot = true;        // gold walked over is picked up (deviations.md #5); off: --toggle autoloot=off

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
    std::string_view sprite_file;        // its FlippyFile, for D2D_ANIM_LOG
    const std::uint8_t* cmap = nullptr;  // ... through the item's colormap (Scene::item_map)
    std::array<std::uint8_t, 3> rgb{ 255, 255, 255 };   // hover label colour
    // A missile: its DCC in direction `dir` (0..31), looping AnimLen frames
    // at AnimSpeed/16 a tick from mode_ms.
    const Scene::MissileInfo* missile = nullptr;
    float rate = 1.f;                    // animation speed (attack speed, FHR, FBR)
    bool hold = false;                   // plays once and stays on its last frame (an opened chest)
    std::uint8_t frames = 0, loop_from = 0;   // an object's FrameCnt / Start in its mode (Npc::frames), 0: the COF's length
    std::uint16_t frame_delta = 0;            // ... and its FrameDelta: 256 a frame a tick
    bool reverse = false;                // plays once backwards and stays on its first frame (a maggot burrowing)
    std::uint16_t get_hit_rate = 0;      // a get-hit (GH): its rate, 8.8 frames a tick (rules::get_hit_rate); rules::get_hit_frame picks the frame, once through and held
    const d2d::dcc::Sprite* overlay = nullptr;   // over it (Overlay.txt npcalert: the quest balloon)
    // Its states' look (Scene::StateInfo): the colour shift (256, before
    // the light) and overlays, each from when it started; `once` plays a
    // cast overlay through a single time.
    const std::uint8_t* shift = nullptr;
    struct Over { const GameData::OverlayInfo* overlay = nullptr; std::uint32_t start = 0; bool once = false; };
    std::vector<Over> overs;
    bool highlight = false;              // under the cursor: drawn at twice its light (FUN_00471ec0)
    bool ghostly = false;                // umod::ghostly: same bright-alpha as `highlight` (unit_draw.c:193)
    int overlay_class = 0;               // Overlay.txt's Height: FUN_006223a0 (players 1, monsters OverlayHeight - 1)
    std::uint16_t says = 0;              // string id over its head (UnitState::says)
    bool shadow = true;                  // a composite casts one, objects too, where its COF layers say (FUN_004dc710 marks every object drawn; FUN_00471620)
    bool object = false;                 // an object: its cell holds half a subtile past game.exe's position, where it's drawn
    bool spirit = false;                 // local co-op's ghost: pale, half see-through, no shadow (docs/design/local-coop.md)
    std::optional<int> name_offset;      // an object's hover name sits this far over its feet, not over its frame (Npc::name_offset)
    std::uint32_t overlay_start = 0;
};


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
// `trans`: a missile's Missiles.txt Trans, its draw mode (0 opaque). True when a pixel landed on screen.
bool blit_dcc_frame(std::vector<std::uint8_t>& framebuffer,
                    const d2d::dcc::Frame& frame,
                    const d2d::palette::Palette& pal,
                    int anchor_x, int anchor_y, int trans = 0);

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
// A transparent COF layer's draw effect as blit_dcc_frame's mode. The
// composite draw (FUN_00470ec0) passes the effect as the draw mode, and
// FUN_006c8250 picks the PL2 table: 0..2 alpha tables 2, 1, 0, a quarter,
// half, three quarters of the layer over the screen; 3 additive, 4
// multiply (like Missiles.txt Trans 1 / 2); 5 opaque (cof-draw-order.md
// "Draw effects", tests/test_cof_effects.cpp).
// ponytail: effect 6 (the max-component table, PL2 +0x5b500) draws opaque.
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
        // Each layer by its own DCC's directions (FUN_005ff760 -> FUN_00600c90 on the DCC's
        // count): a paladin's 8-direction axe in a 16-direction attack. Past its last frame
        // a layer draws nothing (FUN_005feb80 returns no cell): a paladin's tower shield,
        // 8 frames in a 10-frame walk, blinks there in game.exe too (bugs.md #29).
        const auto layer_dir = spr.directions() == dirs ? dir : cof_direction(dir_want, spr.directions());
        if (layer_dir >= spr.directions() || frame >= spr.frames_per_direction()) continue;
        visit(spr.frame(layer_dir, frame), cof_layer(anim, type));
    }
}

// `trans`: every layer's draw mode instead of its own (-1: its own).
void draw_composite(std::vector<std::uint8_t>& framebuffer, const Scene::PlayerAnim& anim,
                    const d2d::palette::Palette& pal, int dir_want,
                    std::uint32_t elapsed_ms, int anchor_x, int anchor_y, int trans = -1);

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
}  // namespace d2d::client
