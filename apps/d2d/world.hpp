// World rendering: DT1 tiles, depth-sorted units, DCC composites.
#pragma once

#include "watchdog.hpp"

namespace {


// Blit one DT1 tile's pre-decoded palette-indexed pixels through `pal`.
// The tile's pixel buffer is (tile.width x abs(tile.height)); index 0 is
// transparent. Screen position is the buffer's top-left; caller does the
// iso math to place it. Bounds-checked per-pixel — off-screen tiles are
// clipped rather than skipped so the compositor can walk the whole grid.
void blit_dt1_tile(std::vector<std::uint8_t>& fb,
                   const d2d::dt1::Tile& t,
                   const d2d::palette::Palette& pal,
                   int sx, int sy) {
    const int th = std::abs(t.height);
    for (int y = 0; y < th; ++y) {
        const int py = sy + y;
        if (py < 0 || py >= int(kH)) continue;
        const auto* row = t.pixels.data() + std::size_t(y) * t.width;
        for (int x = 0; x < t.width; ++x) {
            const std::uint8_t idx = row[x];
            if (idx == 0) continue;   // transparent
            const int px = sx + x;
            if (px < 0 || px >= int(kW)) continue;
            const auto c = pal[idx];
            auto* p = fb.data() + (std::size_t(py) * kW + px) * 4;
            p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = 0xFF;
        }
    }
}

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
    std::array<std::uint8_t, 3> rgb{ 255, 255, 255 };   // hover label colour
    // A missile: its DCC in direction `dir` (0..31), looping AnimLen frames
    // at AnimSpeed/16 a tick from mode_ms.
    const Scene::MissileInfo* missile = nullptr;
    float rate = 1.f;                    // animation speed (attack speed, FHR, FBR)
};

void blit_dcc_frame(std::vector<std::uint8_t>& fb, const d2d::dcc::Frame& f,
                    const d2d::palette::Palette& pal, int anchor_x, int anchor_y);

// The DC6 frame a ground item shows `elapsed` ms after it dropped.
const d2d::dc6::Frame* flippy_frame(const d2d::dc6::Sprite& s, std::uint32_t elapsed) {
    if (s.directions() == 0 || s.frames_per_direction() == 0) return nullptr;
    return &s.frame(0, std::min<std::uint32_t>(elapsed / 40, s.frames_per_direction() - 1));
}

// Screen rectangle a composite's current frame covers with its feet at
// (ax, ay): the union of every drawn layer's frame box. {x0, y0, x1, y1}.
std::array<int, 4> composite_bounds(const Scene::PlayerAnim& p, int dir_want,
                                    std::uint32_t elapsed_ms, int ax, int ay) {
    std::array<int, 4> r{ INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN };
    const auto dirs = p.cof.directions(), fpd = p.cof.frames_per_direction();
    if (dirs == 0 || fpd == 0) return r;
    const auto dir = cof_direction(dir_want, dirs);
    // 25 ticks/s; each tick advances speed/256 frames.
    const auto ms_per_frame = p.ms_per_frame();
    const auto frame = std::uint8_t((elapsed_ms / ms_per_frame) % fpd);
    for (std::size_t t = 0; t < p.dcc.size(); ++t) {
        const auto& spr = p.layer(t);
        if (dir >= spr.directions() || frame >= spr.frames_per_direction()) continue;
        const auto& f = spr.frame(dir, frame);
        r = { std::min(r[0], ax + f.box_left), std::min(r[1], ay + f.box_top),
              std::max(r[2], ax + f.box_right), std::max(r[3], ay + f.box_bottom) };
    }
    return r;
}

void render_world(std::vector<std::uint8_t>& fb,
                  const Scene& s,
                  const Level& L,
                  float cam_x, float cam_y,
                  std::uint32_t elapsed_ms = 0,
                  std::span<const Unit> units = {},
                  int mouse_x = -1, int mouse_y = -1,
                  std::pair<const Unit*, std::array<int, 4>>* hovered = nullptr) {
    const auto& m = L.ds1;
    if (m.width() == 0 || m.height() == 0) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int cx0 = int(kW) / 2;
    const int cy0 = int(kH) / 2;
    const int mw  = m.width();
    const int base_x = int(std::floor(cam_x)), base_y = int(std::floor(cam_y));
    // Screen position of cell (gx, gy)'s top diamond corner. The camera
    // point (cam_x, cam_y) — continuous, in cells — lands at (kW/2,
    // kH/2 + kIsoH/2), i.e. a cell centre when the camera sits on one.
    auto iso = [&](int gx, int gy) {
        const float dx = float(gx) - cam_x, dy = float(gy) - cam_y;
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
    constexpr int kR = 12;

    // Blit a single tile at cell (gx, gy)'s iso position, honouring the
    // 80-tall-diamond-at-bottom convention shared by floor/wall pixel
    // buffers.
    auto blit_cell = [&](int gx, int gy, const d2d::dt1::Tile& t) {
        const auto [iso_x, iso_y] = iso(gx, gy);
        const int th = std::abs(t.height);
        const int sx = iso_x - t.width / 2;
        const int sy = iso_y - (th - kIsoH);
        blit_dt1_tile(fb, t, pal, sx, sy);
    };

    auto find_tile = [&](const Level& lv, int style, int seq, int type)
        -> const d2d::dt1::Tile* {
        const auto it = lv.tile_lookup.find(tile_key(style, seq, type));
        return it == lv.tile_lookup.end() ? nullptr : it->second;
    };
    // Cell (gx, gy) of this level or, past its edge, of the level next to
    // it in the act (Level::near): D2 draws the neighbour's rooms too.
    auto at = [&](int gx, int gy) -> std::pair<const Level*, std::size_t> {
        auto on = [](const Level& lv, int x, int y) {
            return x >= 0 && y >= 0 && x < lv.ds1.width() && y < lv.ds1.height();
        };
        if (on(L, gx, gy)) return { &L, std::size_t(gy) * std::size_t(mw) + std::size_t(gx) };
        for (const auto& n : L.nearby)
            if (on(*n.level, gx - n.dx, gy - n.dy))
                return { n.level, std::size_t(gy - n.dy) * std::size_t(n.level->ds1.width()) + std::size_t(gx - n.dx) };
        return { nullptr, 0 };
    };

    // Row-major sweep so back rows render first. dy increases downward
    // in screen space, so we iterate low→high dy for back-to-front.
    for (int dy = -kR; dy <= kR; ++dy) {
        for (int dx = -kR; dx <= kR; ++dx) {
            const int gx = base_x + dx;
            const int gy = base_y + dy;
            const auto [lv, off] = at(gx, gy);
            if (!lv) continue;
            const auto& cm = lv->ds1;
            if (!lv->picks.empty()) {                   // the tiles game.exe picked: floors, then shadows
                for (const int layer : { 1, 2 })
                    for (const auto& p : lv->picks[off])
                        if (p.layer == layer) blit_cell(gx, gy, *p.tile);
                continue;
            }

            // Floor (single layer typical). Type 0 in the floor stream
            // is the "no floor here" marker (dropped by the game); we
            // still need to look up type=0 for actual floors from DT1s.
            for (const auto& fl : cm.floors()) {
                const auto& c = fl.cells[off];
                // A floor is there when prop1 bit 2 says so (FUN_0066e9b0);
                // (0, 0, 0) with it is the grass tile, without it nothing.
                if (c.hidden || !(c.prop1 & 2)) continue;
                if (auto* t = find_tile(*lv, c.style, c.sequence, /*type=*/0))
                    blit_cell(gx, gy, *t);
            }

            // Shadow layer — 50% alpha decals under characters/objects.
            // For MVP we blit them as regular tiles (index-0 transparent);
            // proper Pl2 blend50 compositing is a follow-up.
            for (const auto& sh : cm.shadows()) {
                const auto& c = sh.cells[off];
                if (c.hidden) continue;
                if (c.style == 0 && c.sequence == 0 && c.wall_type == 0) continue;
                if (auto* t = find_tile(*lv, c.style, c.sequence, /*type=*/13))
                    blit_cell(gx, gy, *t);
            }
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
    // ponytail: cell-granular; D2 sorts units and walls by subtile and
    // wall orientation, which matters once units stand inside a cell's
    // wall line.
    std::vector<const Unit*> order;
    for (const auto& u : units) if (u.anim || u.sprite || u.missile) order.push_back(&u);
    auto diag_of = [&](const Unit* u) {
        return (int(std::floor(u->x)) - base_x) + (int(std::floor(u->y)) - base_y);
    };
    std::ranges::sort(order, {}, [](const Unit* u) { return u->x + u->y; });
    std::size_t next_unit = 0;
    const auto& upal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    auto draw_units_through = [&](int diag) {
        for (; next_unit < order.size() && diag_of(order[next_unit]) <= diag; ++next_unit) {
            const Unit& u = *order[next_unit];
            const auto [ax, ay] = iso_point(u.x, u.y);
            if (ax < -200 || ax > int(kW) + 200 || ay < -100 || ay > int(kH) + 300) continue;
            std::array<int, 4> b{};
            if (u.missile) {
                const auto& spr = *u.missile->dcc;
                const std::uint32_t dirs = spr.directions(), fpd = std::uint32_t(spr.frames_per_direction());
                if (dirs == 0 || fpd == 0) continue;
                const auto frame = (elapsed_ms - u.mode_ms) * std::uint32_t(u.missile->anim_speed) / (40u * 16u)
                                   % std::min<std::uint32_t>(std::uint32_t(u.missile->anim_len), fpd);
                blit_dcc_frame(fb, spr.frame(std::uint8_t(std::uint32_t(u.dir) % dirs), std::uint8_t(frame)), upal, ax, ay);
                continue;
            }
            if (u.sprite) {
                const auto* f = flippy_frame(*u.sprite, elapsed_ms - u.mode_ms);
                if (!f) continue;
                blit_at_anchor(fb, *f, upal, ax, ay);
                b = { ax + f->offset_x, ay + f->offset_y - int(f->height) + 1,
                      ax + f->offset_x + int(f->width), ay + f->offset_y + 1 };
            } else {
                const auto el = std::uint32_t(float(elapsed_ms - u.mode_ms) * u.rate);
                draw_composite(fb, *u.anim, upal, u.dir, el, ax, ay);
                if (hovered && u.name && !u.name->empty()) b = composite_bounds(*u.anim, u.dir, el, ax, ay);
            }
            // Last drawn unit under the cursor = the frontmost one.
            if (hovered && u.name && !u.name->empty()) {
                if (mouse_x >= b[0] && mouse_x < b[2] && mouse_y >= b[1] && mouse_y < b[3])
                    *hovered = { &u, b };
            }
        }
    };
    for (int diag = -2 * kR; diag <= 2 * kR; ++diag) {
        draw_units_through(diag - 1);
        for (int dx = std::max(-kR, diag - kR); dx <= std::min(kR, diag + kR); ++dx) {
            const int dy = diag - dx;
            const int gx = base_x + dx;
            const int gy = base_y + dy;
            const auto [lv, off] = at(gx, gy);
            if (!lv) continue;
            const auto& cm = lv->ds1;
            auto draw_wall = [&](int type, const d2d::dt1::Tile& t) {
                if (type != 15) { blit_cell(gx, gy, t); return; }
                // Roof — hoist by the DT1's own roof_height.
                auto [iso_x, iso_y] = iso(gx, gy);
                iso_y -= t.roof_height;
                blit_dt1_tile(fb, t, pal, iso_x - t.width / 2, iso_y - (std::abs(t.height) - kIsoH));
            };
            if (!lv->picks.empty()) {
                for (const auto& p : lv->picks[off])
                    if (p.layer == 0 && p.orient != 13) draw_wall(p.orient, *p.tile);
                continue;
            }
            for (const auto& wl : cm.walls()) {
                const auto& c = wl.cells[off];
                if (c.hidden) continue;
                const int type = c.wall_type;
                if (type == 0) continue;         // floor marker in wall stream
                if (type == 13) continue;        // shadow (drawn above)
                if (auto* t = find_tile(*lv, c.style, c.sequence, type)) draw_wall(type, *t);
            }
        }
    }
    draw_units_through(1 << 20);
}

// In-game placeholder — a hero has been created; we don't have the actual
// world/map render yet, so celebrate the character info and offer Esc to
// go back to the title. Using the credits bg (dark corridor) as backdrop.
// Blit a DCC frame with its origin at (anchor_x, anchor_y). A DCC frame's
// y_offset is its BOTTOM row relative to the origin (feet), so the pixel
// block's top-left is (box_left, box_top). Palette-indexed; index 0 is
// transparent so limbs compose cleanly over each other and over tiles.
void blit_dcc_frame(std::vector<std::uint8_t>& fb,
                    const d2d::dcc::Frame& f,
                    const d2d::palette::Palette& pal,
                    int anchor_x, int anchor_y) {
    const int dst_x = anchor_x + f.box_left;
    const int dst_y = anchor_y + f.box_top;
    for (std::int32_t y = 0; y < f.height; ++y) {
        const int py = dst_y + y;
        if (py < 0 || py >= int(kH)) continue;
        const auto* row = f.pixels.data() + std::size_t(y) * f.width;
        for (std::int32_t x = 0; x < f.width; ++x) {
            const auto idx = row[x];
            if (idx == 0) continue;
            const int px = dst_x + x;
            if (px < 0 || px >= int(kW)) continue;
            const auto c = pal[idx];
            auto* p = fb.data() + (std::size_t(py) * kW + px) * 4;
            p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = 0xFF;
        }
    }
}

// Draw a composite's current frame with its feet at (anchor_x, anchor_y):
// every loaded layer, in the COF's per-(direction, frame) draw order.
// Frame time from the COF speed byte: D2 advances speed/256 frames per
// 25 Hz tick, so one frame lasts 40 ms * 256 / speed (BA 80 -> 128 ms).
// ponytail: COF speed as the rate; AnimData.d2 is authoritative — read it
// when an animation visibly runs at the wrong pace. No shadow, no
// transparent-layer draw effects yet (no TN layer sets `transparent`).
void draw_composite(std::vector<std::uint8_t>& fb, const Scene::PlayerAnim& p,
                    const d2d::palette::Palette& pal, int dir_want,
                    std::uint32_t elapsed_ms, int anchor_x, int anchor_y) {
    const auto dirs = p.cof.directions();
    const auto fpd  = p.cof.frames_per_direction();
    if (dirs == 0 || fpd == 0) return;
    const auto dir = cof_direction(dir_want, dirs);
    // 25 ticks/s; each tick advances speed/256 frames.
    const auto ms_per_frame = p.ms_per_frame();
    const auto frame = std::uint8_t((elapsed_ms / ms_per_frame) % fpd);
    for (const auto type : p.cof.priority(dir, frame)) {
        if (type >= p.dcc.size()) continue;
        const auto& spr = p.layer(type);
        if (dir >= spr.directions() || frame >= spr.frames_per_direction()) continue;
        blit_dcc_frame(fb, spr.frame(dir, frame), pal, anchor_x, anchor_y);
    }
}

}  // namespace
