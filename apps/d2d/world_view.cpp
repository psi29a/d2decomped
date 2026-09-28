// Definitions for world_view.hpp: drawing the world: tiles, walls, units, lights.
#include "world_view.hpp"
#include "common.hpp"
#include "scene.hpp"
#include "watchdog.hpp"

namespace d2d::client {

void blit_dt1_tile(std::vector<std::uint8_t>& fb,
                   const d2d::dt1::Tile& t,
                   const d2d::palette::Palette& pal,
                   int sx, int sy, int alpha_all , const Hole* hole) {
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
            const int alpha = hole ? hole->alpha(px, py, alpha_all) : alpha_all;
            if (alpha >= 255) { p[0] = c.r; p[1] = c.g; p[2] = c.b; }
            else { p[0] = std::uint8_t((p[0] * (255 - alpha) + c.r * alpha) / 255); p[1] = std::uint8_t((p[1] * (255 - alpha) + c.g * alpha) / 255); p[2] = std::uint8_t((p[2] * (255 - alpha) + c.b * alpha) / 255); }
            p[3] = 0xFF;
        }
    }
}

void blit_dt1_shadow(std::vector<std::uint8_t>& fb, const d2d::dt1::Tile& t,
                     const d2d::palette::Palette& pal, int sx, int sy) {
    const int th = std::abs(t.height);
    for (int y = 0; y < th; ++y) {
        const int py = sy + y;
        if (py < 0 || py >= int(kH)) continue;
        const auto* row = t.pixels.data() + std::size_t(y) * t.width;
        for (int x = 0; x < t.width; ++x) {
            const std::uint8_t idx = row[x];
            const int px = sx + x;
            if (idx == 0 || px < 0 || px >= int(kW)) continue;
            const auto c = pal[idx];
            auto* p = fb.data() + (std::size_t(py) * kW + px) * 4;
            p[0] = std::uint8_t((p[0] * 3 + c.r) / 4); p[1] = std::uint8_t((p[1] * 3 + c.g) / 4); p[2] = std::uint8_t((p[2] * 3 + c.b) / 4);
        }
    }
}

void blit_dt1_tile_lit(std::vector<std::uint8_t>& fb, const d2d::dt1::Tile& t, const Lighting& light,
                       int sx, int sy, int gx, int gy, int top_x, int top_y, bool floor, int alpha_all , const Hole* hole) {
    // The subtile corners round the cell: a tile's pixels reach up to 6
    // subtiles before its top corner (tall floors) and 8 past.
    constexpr int kP = 16, kO = 6;
    std::array<float, kP * kP> c{};
    int lo = 255, hi = 0;
    for (int j = 0; j < kP; ++j)
        for (int i = 0; i < kP; ++i) {
            const int v = light.grid.at(gx * 5 - kO + i, gy * 5 - kO + j);
            c[std::size_t(j * kP + i)] = float(v);
            lo = std::min(lo, v); hi = std::max(hi, v);
        }
    const auto& pals = *light.pal;
    if (lo >> 3 == hi >> 3) { blit_dt1_tile(fb, t, pals[std::size_t(lo >> 3)], sx, sy, alpha_all, hole); return; }
    // Screen offset from the top corner → subtiles into the cell: dx - dy =
    // x / 80 cells, dx + dy = y / 40.
    auto level = [&](int ox, int oy) {
        const float p = float(ox) / (kIsoW / 2), q = float(oy) / (kIsoH / 2);
        const float a = std::clamp((q + p) * 2.5f + kO, 0.f, kP - 1.001f), b = std::clamp((q - p) * 2.5f + kO, 0.f, kP - 1.001f);
        const int i = int(a), j = int(b);
        const float fa = a - float(i), fb2 = b - float(j);
        const float* e = &c[std::size_t(j * kP + i)];
        return std::size_t(int((e[0] + (e[1] - e[0]) * fa) * (1 - fb2) + (e[kP] + (e[kP + 1] - e[kP]) * fa) * fb2) >> 3);
    };
    const int th = std::abs(t.height);
    for (int y = 0; y < th; ++y) {
        const int py = sy + y;
        if (py < 0 || py >= int(kH)) continue;
        const auto* row = t.pixels.data() + std::size_t(y) * t.width;
        for (int x = 0; x < t.width; ++x) {
            const std::uint8_t idx = row[x];
            if (idx == 0) continue;
            const int px = sx + x;
            if (px < 0 || px >= int(kW)) continue;
            const auto lvl = level(px - top_x, floor ? py - top_y : kIsoH / 2);
            const auto col = pals[lvl][idx];
            auto* p = fb.data() + (std::size_t(py) * kW + px) * 4;
            const int alpha = hole ? hole->alpha(px, py, alpha_all) : alpha_all;
            if (alpha >= 255) { p[0] = col.r; p[1] = col.g; p[2] = col.b; }
            else { p[0] = std::uint8_t((p[0] * (255 - alpha) + col.r * alpha) / 255); p[1] = std::uint8_t((p[1] * (255 - alpha) + col.g * alpha) / 255); p[2] = std::uint8_t((p[2] * (255 - alpha) + col.b * alpha) / 255); }
            p[3] = 0xFF;
        }
    }
}

const d2d::dc6::Frame* flippy_frame(const d2d::dc6::Sprite& s, std::uint32_t elapsed) {
    if (s.directions() == 0 || s.frames_per_direction() == 0) return nullptr;
    return &s.frame(0, std::min<std::uint32_t>(elapsed / 40, s.frames_per_direction() - 1));
}

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
                  std::uint32_t elapsed_ms ,
                  std::span<const Unit> units ,
                  int mouse_x , int mouse_y ,
                  std::pair<const Unit*, std::array<int, 4>>* hovered ,
                  const Lighting* light , d2d::rules::Rain* rain ,
                  std::vector<std::pair<const Unit*, std::array<int, 4>>>* items) {   // each ground item drawn, its box
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
    // `layer`: 0 wall, 1 floor, 2 shadow (the pass, not the DT1's own type field).
    auto blit_cell = [&](int gx, int gy, const d2d::dt1::Tile& t, int layer, int alpha = 255) {
        const auto [iso_x, iso_y] = iso(gx, gy);
        const int th = std::abs(t.height);
        const int sx = iso_x - t.width / 2;
        const int sy = iso_y - (th - kIsoH);
        if (layer == 2) blit_dt1_shadow(fb, t, pal, sx, sy);
        else if (light) blit_dt1_tile_lit(fb, t, *light, sx, sy, gx, gy, iso_x, iso_y, layer == 1, alpha);
        else blit_dt1_tile(fb, t, pal, sx, sy, alpha);
    };
    // Walls in front of the player see-through (FUN_004dd060 /
    // FUN_004dd180): a wall 1..3 cells past the player's cell in x
    // (orientations 1 4 5 7 8 10 12) or in y (2 3 6 7 9 11 12) fades to
    // alpha 0x80 over 500 ms, and back to 0xff once it isn't. Roofs (15)
    // and lower walls (16..19) never fade.
    // ponytail: the blend is linear in RGB, not the driver's alpha table;
    // game.exe's room-based mode (DAT_0072a968) isn't built.
    struct Fade { int from = 255, to = 255; std::uint32_t at = 0; };
    static std::unordered_map<std::uint64_t, Fade> fades;
    const int pcx = int(std::floor(cam_x)), pcy = int(std::floor(cam_y));
    const auto [hole_x, hole_y] = iso_point(cam_x, cam_y);
    const Hole hole{ hole_x, hole_y - 40, 70 };           // round the player's body
    auto wall_alpha = [&](const void* lv, int off, int type, int k, int gx, int gy) {
        static constexpr std::uint16_t kX = 1 << 1 | 1 << 4 | 1 << 5 | 1 << 7 | 1 << 8 | 1 << 10 | 1 << 12;
        static constexpr std::uint16_t kY = 1 << 2 | 1 << 3 | 1 << 6 | 1 << 7 | 1 << 9 | 1 << 11 | 1 << 12;
        const bool see = type < 16 && ((gx > pcx && gx < pcx + 4 && (kX >> type & 1)) || (gy > pcy && gy < pcy + 4 && (kY >> type & 1)));
        const int want = see ? 0x80 : 0xff;
        const auto key = std::uint64_t(reinterpret_cast<std::uintptr_t>(lv)) * 1000003u ^ (std::uint64_t(off) << 12 | std::uint64_t(type) << 6 | std::uint64_t(k));
        auto it = fades.find(key);
        if (it == fades.end()) { if (!see) return 255; it = fades.emplace(key, Fade{ 255, 255, elapsed_ms }).first; }
        auto& f = it->second;
        const auto now_a = [&] {                // 0x7f every 500 ms, from where it was
            const int step = int(std::min<std::uint32_t>(elapsed_ms - f.at, 1000)) * 0x7f / 500;
            return f.to > f.from ? std::min(f.to, f.from + step) : std::max(f.to, f.from - step);
        };
        if (f.to != want) { f.from = now_a(); f.to = want; f.at = elapsed_ms; }
        const int a = now_a();
        if (a >= 255 && !see) fades.erase(it);
        return a;
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

    const auto& upal_splash = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    // Row-major sweep so back rows render first. dy increases downward
    // in screen space, so we iterate low→high dy for back-to-front.
    for (int dy = -kR; dy <= kR; ++dy) {
        for (int dx = -kR; dx <= kR; ++dx) {
            const int gx = base_x + dx;
            const int gy = base_y + dy;
            const auto [lv, off] = at(gx, gy);
            if (!lv) continue;
            const auto& cm = lv->ds1;
            // A floor whose DT1 material flags have 2 may splash in the rain
            // (FUN_004de410, as the tile's drawn).
            auto splash = [&](const d2d::dt1::Tile& t) {
                if (rain && (t.material_flags & 2)) { const auto [x, y] = iso(gx, gy); rain->floor(rain->rng, x, y); }
            };
            if (!lv->picks.empty()) {                   // the tiles game.exe picked: floors, then shadows
                for (const int layer : { 1, 2 })
                    for (const auto& p : lv->picks[off])
                        if (p.layer == layer) { blit_cell(gx, gy, *p.tile, layer); if (layer == 1) splash(*p.tile); }
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
                if (auto* t = find_tile(*lv, c.style, c.sequence, /*type=*/0)) {
                    blit_cell(gx, gy, *t, 1);
                    splash(*t);
                }
            }

            // Shadow layer: blended over the floor (blit_dt1_shadow).
            for (const auto& sh : cm.shadows()) {
                const auto& c = sh.cells[off];
                if (c.hidden) continue;
                if (c.style == 0 && c.sequence == 0 && c.wall_type == 0) continue;
                if (auto* t = find_tile(*lv, c.style, c.sequence, /*type=*/13))
                    blit_cell(gx, gy, *t, 2);
            }
        }
    }

    // The rain's splashes on the floor (FUN_00473c00 → FUN_00473a70: draw
    // mode 3, additive).
    if (rain)
        for (const auto& sp : rain->splashes)
            if (const auto fr = s.rain_splash[std::size_t(sp.kind)].frames(); sp.frame < int(fr.size()))
                blit_additive(fb, fr[std::size_t(sp.frame)], upal_splash, nullptr, sp.x, sp.y);
    // Units' shadows, on the ground under the walls and units (the floor
    // pass FUN_004df510 → FUN_004dc7b0).
    {
        static std::vector<std::uint16_t> mask(std::size_t(kW) * kH, 0);
        static std::uint16_t id = 0;
        for (const auto& u : units) {
            if (!u.anim || !u.shadow) continue;
            const auto [ax, ay] = iso_point(u.x, u.y);
            if (ax < -200 || ax > int(kW) + 200 || ay < -100 || ay > int(kH) + 300) continue;
            if (++id == 0) { std::ranges::fill(mask, std::uint16_t{ 0 }); id = 1; }
            shadow_composite(fb, *u.anim, u.dir, std::uint32_t(float(elapsed_ms - u.mode_ms) * u.rate), ax, ay, mask, id);
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
    for (const auto& u : units) if (u.anim || u.sprite || u.missile) order.push_back(&u);
    auto diag_of = [&](const Unit* u) {
        return (int(std::floor(u->x)) - base_x) + (int(std::floor(u->y)) - base_y);
    };
    std::ranges::sort(order, {}, [](const Unit* u) { return u->x + u->y; });
    std::size_t next_unit = 0;
    const auto& upal0 = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    auto draw_units_through = [&](int diag) {
        for (; next_unit < order.size() && diag_of(order[next_unit]) <= diag; ++next_unit) {
            const Unit& u = *order[next_unit];
            const auto [ax, ay] = iso_point(u.x, u.y);
            // The unit under the cursor (FUN_00467a10) at twice its light,
            // 0x40..0xff (FUN_00471ec0).
            // ponytail: objects also switch to draw mode 7 there, untraced.
            const auto& lpal = !light ? upal0
                             : (*light->pal)[std::size_t((u.highlight ? std::clamp(light->unit_at(u.x, u.y) * 2, 0x40, 0xff) : light->unit_at(u.x, u.y)) >> 3)];
            // A state's colour shift wins over the unit's own colour
            // (a monster's palshift / RandTransforms, an item's colormap).
            const auto* umap = u.shift ? u.shift : u.cmap;
            const auto spal = umap ? Scene::mapped(lpal, umap) : d2d::palette::Palette{};
            const auto& upal = umap ? spal : lpal;
            // Its overlays: PreDraw ones behind it, the rest in front
            // (FUN_00470390: AnimRate x 16 / 256 frames a tick).
            auto draw_overs = [&](bool pre) {
                for (const auto& ov : u.overs) {
                    if (ov.o->predraw != pre) continue;
                    const auto* spr = s.overlay_sprite(*ov.o);
                    if (!spr || elapsed_ms < ov.start) continue;
                    const auto n = std::uint32_t(std::min(ov.o->frames, int(spr->frames_per_direction())));
                    auto f = (elapsed_ms - ov.start) * std::uint32_t(std::max(ov.o->rate, 1)) / 640;
                    if (ov.once && f >= n) continue;
                    f %= n;
                    blit_dcc_frame(fb, spr->frame(std::uint8_t(std::uint32_t(u.dir) % spr->directions()), std::int32_t(f)), upal0,
                                   ax + ov.o->x, ay + ov.o->dy(u.overlay_class), draw_mode(ov.o->trans));
                }
            };
            draw_overs(true);
            if (ax < -200 || ax > int(kW) + 200 || ay < -100 || ay > int(kH) + 300) continue;
            std::array<int, 4> b{};
            if (u.missile) {
                const auto cel = s.missile_cels.find(u.missile->name);
                if (cel == s.missile_cels.end()) continue;
                const auto& spr = cel->second;
                const std::uint32_t dirs = spr.directions(), fpd = std::uint32_t(spr.frames_per_direction());
                if (dirs == 0 || fpd == 0) continue;
                const auto frame = (elapsed_ms - u.mode_ms) * std::uint32_t(u.missile->anim_speed) / (40u * 16u)
                                   % std::min<std::uint32_t>(std::uint32_t(u.missile->anim_len), fpd);
                blit_dcc_frame(fb, spr.frame(std::uint8_t(std::uint32_t(u.dir) % dirs), std::uint8_t(frame)), u.missile->trans ? upal0 : upal, ax, ay, u.missile->trans);
                continue;
            }
            if (u.sprite) {
                const auto* f = flippy_frame(*u.sprite, elapsed_ms - u.mode_ms);
                if (!f) continue;
                blit_at_anchor(fb, *f, upal, ax, ay);
                b = { ax + f->offset_x, ay + f->offset_y - int(f->height) + 1,
                      ax + f->offset_x + int(f->width), ay + f->offset_y + 1 };
                if (items) items->push_back({ &u, b });
            } else {
                const auto el = std::uint32_t(float(elapsed_ms - u.mode_ms) * u.rate);
                draw_composite(fb, *u.anim, upal, u.dir, el, ax, ay);
                if (hovered && u.name && !u.name->empty()) b = composite_bounds(*u.anim, u.dir, el, ax, ay);
            }
            draw_overs(false);
            // Its overlay (npcalert: Xoffset -5, Yoffset -7, the NPCs'
            // OverlayHeight row 0; Trans 3, draw mode 3 additive), 16
            // frames at AnimRate 9.
            // ponytail: AnimRate read as frames a second; LoopWaitTime
            // (7000) not applied.
            if (u.overlay && u.overlay->directions() && u.overlay->frames_per_direction())
                blit_dcc_frame(fb, u.overlay->frame(0, std::uint8_t(elapsed_ms / 111 % u.overlay->frames_per_direction())), upal0, ax - 5, ay - 7, 1);
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
            auto draw_wall = [&](int type, const d2d::dt1::Tile& t, int k) {
                if (type != 15) { blit_cell(gx, gy, t, 0, wall_alpha(lv, int(off), type, k, gx, gy)); return; }
                // Roof — hoist by the DT1's own roof_height.
                auto [iso_x, iso_y] = iso(gx, gy);
                iso_y -= t.roof_height;
                // A roof is flat: lit like a floor, where each pixel lies on
                // the roof's plane (its top corner at the hoisted iso_y).
                const Hole* h = g_roof_cutout ? &hole : nullptr;
                if (light) blit_dt1_tile_lit(fb, t, *light, iso_x - t.width / 2, iso_y - (std::abs(t.height) - kIsoH), gx, gy, iso_x, iso_y, true, 255, h);
                else blit_dt1_tile(fb, t, pal, iso_x - t.width / 2, iso_y - (std::abs(t.height) - kIsoH), 255, h);
            };
            if (!lv->picks.empty()) {
                int k = 0;
                for (const auto& p : lv->picks[off])
                    if (p.layer == 0 && p.orient != 13) draw_wall(p.orient, *p.tile, k++);
                continue;
            }
            int k = 0;
            for (const auto& wl : cm.walls()) {
                const auto& c = wl.cells[off];
                ++k;
                if (c.hidden) continue;
                const int type = c.wall_type;
                if (type == 0) continue;         // floor marker in wall stream
                if (type == 13) continue;        // shadow (drawn above)
                if (auto* t = find_tile(*lv, c.style, c.sequence, type)) draw_wall(type, *t, k);
            }
        }
    }
    draw_units_through(1 << 20);
}

void blit_dcc_frame(std::vector<std::uint8_t>& fb,
                    const d2d::dcc::Frame& f,
                    const d2d::palette::Palette& pal,
                    int anchor_x, int anchor_y, int trans) {
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
            if (trans == 1) { p[0] = std::uint8_t(std::min(255, p[0] + c.r)); p[1] = std::uint8_t(std::min(255, p[1] + c.g)); p[2] = std::uint8_t(std::min(255, p[2] + c.b)); }
            else if (trans == 2) { p[0] = std::uint8_t(p[0] * c.r / 255); p[1] = std::uint8_t(p[1] * c.g / 255); p[2] = std::uint8_t(p[2] * c.b / 255); }
            else if (trans >= 3 && trans <= 5) {                       // a quarter, half, three quarters of the layer
                const int a = trans - 2;
                p[0] = std::uint8_t((p[0] * (4 - a) + c.r * a) / 4); p[1] = std::uint8_t((p[1] * (4 - a) + c.g * a) / 4); p[2] = std::uint8_t((p[2] * (4 - a) + c.b * a) / 4);
            }
            else { p[0] = c.r; p[1] = c.g; p[2] = c.b; }
            p[3] = 0xFF;
        }
    }
}

void shadow_composite(std::vector<std::uint8_t>& fb, const Scene::PlayerAnim& p, int dir_want, std::uint32_t elapsed_ms,
                      int anchor_x, int anchor_y, std::vector<std::uint16_t>& mask, std::uint16_t id) {
    composite_frames(p, dir_want, elapsed_ms, [&](const d2d::dcc::Frame& f, const d2d::cof::Layer* l) {
        if (l && !l->shadow) return;                               // the COF says this layer casts none
        const int bottom = f.box_top + f.height - 1;              // the frame's bottom row, from the anchor
        const int x0 = anchor_x + f.box_left + bottom / 2, y0 = anchor_y + bottom / 2;
        for (std::int32_t r = 0; r < f.height; r += 2) {          // rows up from the bottom
            const int py = y0 - r / 2;
            if (py < 0 || py >= int(kH)) continue;
            const auto* row = f.pixels.data() + std::size_t(f.height - 1 - r) * f.width;
            for (std::int32_t x = 0; x < f.width; ++x) {
                const int px = x0 + x - r / 2;
                if (row[x] == 0 || px < 0 || px >= int(kW)) continue;
                auto& m = mask[std::size_t(py) * kW + std::size_t(px)];
                if (m == id) continue;
                m = id;
                auto* q = fb.data() + (std::size_t(py) * kW + std::size_t(px)) * 4;
                q[0] = std::uint8_t(q[0] / 4); q[1] = std::uint8_t(q[1] / 4); q[2] = std::uint8_t(q[2] / 4);
            }
        }
    });
}

void draw_composite(std::vector<std::uint8_t>& fb, const Scene::PlayerAnim& p,
                    const d2d::palette::Palette& pal, int dir_want,
                    std::uint32_t elapsed_ms, int anchor_x, int anchor_y) {
    composite_frames(p, dir_want, elapsed_ms, [&](const d2d::dcc::Frame& f, const d2d::cof::Layer* l) {
        blit_dcc_frame(fb, f, pal, anchor_x, anchor_y, layer_trans(l));
    });
}

}  // namespace d2d::client
