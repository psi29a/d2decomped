// In-game panels: inventory, character, HUD, stash/cube, belt, automap, waypoints.
#pragma once

#include "items.hpp"

namespace {

// The inventory panel (inventory.txt "<Class>2" layout, invchar6.dc6):
// grid items (panel 1) centred in their w x h cell block, equipped items
// centred in their body slot's box. Palette: the act's, like the world.
// The item under (mx, my) gets its hover text.
// ponytail: no colour tints (item transform colormaps), cube.
void draw_inventory(std::vector<std::uint8_t>& fb, const Scene& s, const Scene::InvLayout& L,
                    const std::vector<d2d::d2s::Item>& items, int mx = -1, int my = -1, int clvl = 1,
                    const std::function<std::string(const d2d::d2s::Item&)>* price = nullptr) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    // invchar6.dc6 holds two 2x2 panels (256+64 wide, 256+176 tall);
    // frames 4..7 are the inventory, 0..3 the character-stats page.
    if (s.inv_panel.frames_per_direction() >= 8) {
        const auto& f0 = s.inv_panel.frame(0, 4);
        blit_sprite(fb, f0, pal, L.panel_x, L.panel_y);
        blit_sprite(fb, s.inv_panel.frame(0, 5), pal, L.panel_x + int(f0.width), L.panel_y);
        blit_sprite(fb, s.inv_panel.frame(0, 6), pal, L.panel_x, L.panel_y + int(f0.height));
        blit_sprite(fb, s.inv_panel.frame(0, 7), pal, L.panel_x + int(f0.width), L.panel_y + int(f0.height));
    }
    auto draw_in = [&](const d2d::d2s::Item& it, int x, int y, int w, int h) {
        const auto* spr = s.item_sprite(it);
        if (!spr || spr->frames_per_direction() == 0) return;
        const auto& f = spr->frame(0, 0);
        blit_sprite(fb, f, pal, x + (w - int(f.width)) / 2, y + (h - int(f.height)) / 2);
    };
    const d2d::d2s::Item* hover = nullptr;
    std::array<int, 4> hover_box{};
    for (const auto& it : items) {
        std::array<int, 4> r{};
        if (it.location == 0 && it.panel == 1) {
            const auto info = s.rules.item_info.find(it.code);
            const int iw = info != s.rules.item_info.end() ? info->second.w : 1;
            const int ih = info != s.rules.item_info.end() ? info->second.h : 1;
            r = { L.grid_x + it.column * L.box_w, L.grid_y + it.row * L.box_h, iw * L.box_w, ih * L.box_h };
        } else if (it.location == 1 && it.slot >= 1 && it.slot <= 10) {
            r = L.slots[std::size_t(it.slot)];
        }
        if (r[2] <= 0) continue;
        draw_in(it, r[0], r[1], r[2], r[3]);
        if (mx >= r[0] && mx < r[0] + r[2] && my >= r[1] && my < r[1] + r[3]) { hover = &it; hover_box = r; }
    }
    if (hover) {
        auto lines = item_lines(s, *hover, clvl);
        if (price && *price) lines.push_back({ (*price)(*hover), kTxtWhite });
        draw_hover_text(fb, s, lines, hover_box[0], hover_box[0] + hover_box[2],
                        hover_box[1] + hover_box[3], hover_box[1]);
    }
}

// Character panel (left of the inventory: 800x600 puts the left panels
// at 80..400). Art: invchar6.dc6 frames 0..3. Label and value boxes are
// game.exe's own tables — labels {x0, y, x1, string id} at 0x724818
// (18-byte records), values {x0, y, x1, stat id} at 0x724928 — in panel
// coordinates; text is centred in [x0, x1].
constexpr int kCharPanelX = 80, kCharPanelY = 60;
struct PanelText { int x0, y, x1, id; };
constexpr PanelText kCharLabels[] = {
    {  11,  44,  52, 0xfd9 }, {  65,  44, 180, 0xfda }, { 193,  44, 308, 0xfdb },
    {  10,  97,  73, 0xfdc }, {  10, 160,  73, 0xfde }, { 174, 207, 268, 0xfe0 },
    {  10, 245,  73, 0xfe2 }, { 174, 245, 228, 0xfe3 }, { 174, 269, 228, 0xfe4 },
    {  10, 307,  73, 0xfe5 }, { 174, 307, 228, 0xfe6 }, { 190, 346, 268, 0xfe7 },
    { 190, 370, 268, 0xfe8 }, { 190, 395, 268, 0xfe9 }, { 190, 419, 268, 0xfea },
};
constexpr PanelText kCharValues[] = {
    {  13,  59,  53, 12 }, {  67,  59, 180, 13 }, { 195,  59, 308, 30 },
    {  77,  99, 112,  0 }, {  77, 161, 112,  2 }, { 273, 209, 307, 31 },
    {  77, 247, 112,  3 }, { 232, 246, 267, 11 }, { 273, 246, 308, 10 },
    { 232, 270, 267,  7 }, { 273, 270, 308,  6 }, {  77, 308, 112,  1 },
    { 232, 308, 267,  9 }, { 273, 308, 308,  8 }, { 273, 348, 307, 39 },
    { 273, 372, 307, 43 }, { 273, 396, 307, 41 }, { 273, 420, 307, 45 },
};

// Stat point buttons, from game.exe's table at 0x724a48 (14-byte records
// {u32 x, u32 y, u32 pressed, u16 stat}): the button's bottom-left in panel
// coordinates, and the stat a click spends on. Hit box x in (x, x+40),
// y in (y-22, y) (FUN_004a7720 / FUN_004a78c0).
struct StatButton { int x, y, stat; };
constexpr StatButton kStatButtons[4] = { { 117, 105, 0 }, { 117, 167, 2 }, { 117, 253, 3 }, { 117, 315, 1 } };

// The stat button under (mx, my) (screen), or -1.
int stat_button_at(int mx, int my) {
    for (int i = 0; i < 4; ++i) {
        const auto& b = kStatButtons[i];
        const int x = mx - kCharPanelX, y = my - kCharPanelY;
        if (x > b.x && x < b.x + 40 && y > b.y - 22 && y < b.y) return i;
    }
    return -1;
}

void draw_char_panel(std::vector<std::uint8_t>& fb, const Scene& s, const d2d::d2s::Stats& st,
                     const PanelStats& ps,
                     std::string_view name, int class_idx, int pressed_button = -1) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int px = kCharPanelX, py = kCharPanelY;
    if (s.inv_panel.frames_per_direction() >= 8) {
        const auto& f0 = s.inv_panel.frame(0, 0);
        blit_sprite(fb, f0, pal, px, py);
        blit_sprite(fb, s.inv_panel.frame(0, 1), pal, px + int(f0.width), py);
        blit_sprite(fb, s.inv_panel.frame(0, 2), pal, px, py + int(f0.height));
        blit_sprite(fb, s.inv_panel.frame(0, 3), pal, px + int(f0.width), py + int(f0.height));
    }
    // As FUN_004a7d00 draws it: text centred in [x0, x1] as
    // x0 + (x1 - x0 + 1 - w) / 2 (left-aligned when it doesn't fit), y the
    // baseline. Labels in font6, a "Fire\nResistance" pair at y-4 / y+4;
    // values in font16, dropping to font8 when > 999 or too wide (life/
    // mana/stamina); name font16/font8/font6 by length, class font16.
    // ponytail: all white; the game colours boosted/lowered values blue/red
    // and maxed resistances gold (local_8 in FUN_004a7d00).
    auto pick = [&](const d2d::font::Font& f) -> const d2d::font::Font& {
        return f.line_height() > 0 ? f : s.font;
    };
    const auto& f16 = s.font;
    const auto& f8 = pick(s.font_small);
    const auto& f6 = pick(s.font_tiny);
    auto text = [&](const d2d::font::Font& f, int x0, int x1, int y, const std::string& t) {
        const int w = f.measure(t);
        const int x = w < x1 - x0 + 1 ? x0 + (x1 - x0 + 1 - w) / 2 : x0;
        // Glyph cells blit bottom-anchored at y, like any DC6 (font6 cells
        // are 11 tall with the baseline on row 8).
        f.draw(fb, kW, kH, pal, px + x, py + y - int(f.sheet().frame(0, 0).height) + 1, t);
    };
    for (const auto& t : kCharLabels) {
        const auto v = lookup_string(s, std::uint16_t(t.id));
        if (!v) continue;
        const auto txt = u16_to_latin1(*v);
        if (const auto nl = txt.find('\n'); nl != txt.npos) {
            text(f6, t.x0, t.x1, t.y - 4, txt.substr(0, nl));
            text(f6, t.x0, t.x1, t.y + 4, txt.substr(nl + 1));
        } else {
            text(f6, t.x0, t.x1, t.y, txt);
        }
    }
    for (const auto& t : kCharValues) {
        const bool fixed = t.id >= 6 && t.id <= 11;       // life/mana/stamina, 8.8
        std::int64_t v = fixed ? st.fixed(t.id) : st.get(t.id);
        switch (t.id) {
            case 30: v = ps.next; break;
            case 31: v = ps.defense; break;
            case 39: v = ps.res[0]; break;
            case 43: v = ps.res[1]; break;
            case 41: v = ps.res[2]; break;
            case 45: v = ps.res[3]; break;
            default: break;
        }
        if (t.id == 30 && v < 0) continue;                // max level: blank
        const auto txt = std::to_string(v);
        const bool small = (fixed || t.id == 31) && (v > 999 || f16.measure(txt) >= t.x1 - t.x0);
        text(small ? f8 : f16, t.x0, t.x1, t.y, txt);
    }
    std::string cls = class_idx >= 0 && class_idx < 7 ? kClassKey[class_idx] : "";
    if (auto v = lookup_string(s, cls)) cls = u16_to_latin1(*v);
    const auto& fname = name.size() + 1 <= 11 ? f16 : name.size() + 1 < 14 ? f8 : f6;
    text(fname, 13, 13 + 0xa1 - 0xd - 1, 25, std::string(name));
    text(f16, 0xc1, 0x137 - 1, 25, cls);
    // Unspent stat points (FUN_004a7d00): the skillpoints box at (3, 364)
    // with "Stat Points" / "Remaining" (0xfeb, 0xfec) in font6 centred in
    // 11..88 at baselines 355 / 363 and the count in font16 in 92..127 at
    // 360; each stat's button, levelsocket at (x+5, y+5) under level
    // (frame 1 while pressed) at (x+8, y+1). DC6s anchor bottom-left.
    if (const auto pts = st.get(d2d::d2s::kStatPts); pts > 0) {
        auto dc6 = [&](const d2d::dc6::Sprite& spr, int frame, int x, int y) {
            if (frame < 0 || spr.frames_per_direction() <= std::uint32_t(frame)) return;
            const auto& f = spr.frame(0, std::uint32_t(frame));
            blit_sprite(fb, f, pal, px + x, py + y - int(f.height) + 1);
        };
        dc6(s.points_box, 0, 3, 364);
        for (auto [id, y] : { std::pair{ 0xfeb, 355 }, { 0xfec, 363 } })
            if (auto v = lookup_string(s, std::uint16_t(id))) text(f6, 11, 0x59 - 1, y, u16_to_latin1(*v));
        text(f16, 0x5c, 0x80 - 1, 360, std::to_string(pts));
        for (int i = 0; i < 4; ++i) {
            const auto& b = kStatButtons[i];
            dc6(s.level_socket, 0, b.x + 5, b.y + 5);
            dc6(s.level_button, i == pressed_button ? 1 : 0, b.x + 8, b.y + 1);
        }
    }
}

// The bottom HUD, as game.exe's 800x600 path draws it (FUN_004983d0 for
// the bar, FUN_00496f80 / FUN_00497110 for the globes). All cels are
// bottom-anchored on the screen's bottom edge:
//   bar: frame 0 (life housing) at x 0, frames 1..4 at 400-235, -107,
//   +21, +149, frame 5 (mana housing) at 800-117; the 48px gaps are the
//   skill buttons.
//   globes: fill = cur * 80 / max rows of hlthmana frame 0 (life; 2 when
//   poisoned) / 1 (mana), bottom at H-13, x 29 / W-111; then the glass
//   (overlap frame 0 at x 28, bottom H-5; frame 1 at W-110, bottom H-9).
// ponytail: saved life/mana are base values (no item bonuses); no
// poison tint, stamina bar, skill icons or run/walk yet.
void draw_hud(std::vector<std::uint8_t>& fb, const Scene& s, const d2d::d2s::Stats& st) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int W = int(kW), H = int(kH);
    auto at_bottom = [&](const d2d::dc6::Sprite& spr, int frame, int x, int bottom) {
        if (frame >= int(spr.frames_per_direction())) return;
        const auto& f = spr.frame(0, std::uint32_t(frame));
        blit_sprite(fb, f, pal, x, bottom - int(f.height));
    };
    if (s.ctrl_panel.frames_per_direction() >= 6) {
        const int xs[6] = { 0, W / 2 - 0xeb, W / 2 - 0x6b, W / 2 + 0x15, W / 2 + 0x95, W - 0x75 };
        for (int i = 0; i < 6; ++i) at_bottom(s.ctrl_panel, i, xs[i], H);
    }
    // Globe fill: only the bottom `rows` rows of the 80x80 cel.
    auto fill = [&](int frame, int x, std::int64_t cur, std::int64_t max) {
        if (max <= 0 || frame >= int(s.globes.frames_per_direction())) return;
        const auto& f = s.globes.frame(0, std::uint32_t(frame));
        const int rows = int(std::clamp<std::int64_t>(cur * 80 / max, 0, 80));
        const int top = H - 13 - int(f.height);
        for (int y = int(f.height) - rows; y < int(f.height); ++y)
            for (int x0 = 0; x0 < int(f.width); ++x0) {
                const auto idx = f.pixels[std::size_t(y) * f.width + std::size_t(x0)];
                const int px = x + x0, py = top + y;
                if (!idx || px < 0 || py < 0 || px >= W || py >= H) continue;
                const auto c = pal[idx];
                auto* d = fb.data() + (std::size_t(py) * kW + std::size_t(px)) * 4;
                d[0] = c.r; d[1] = c.g; d[2] = c.b;
            }
    };
    fill(0, 29, st.fixed(d2d::d2s::kLife), st.fixed(d2d::d2s::kMaxLife));
    fill(1, W - 0x6f, st.fixed(d2d::d2s::kMana), st.fixed(d2d::d2s::kMaxMana));
    at_bottom(s.globe_glass, 0, 28, H - 5);
    at_bottom(s.globe_glass, 1, W - 0x6e, H - 9);
}

// The stash panel: art frames 0..3 as 2x2 at the left-panel spot, items
// (location 0, panel 5) in the inventory.txt bank grid.
// ponytail: no gold line, no "close" button; classic stash untested.
// Rect {x, y, w, h} of a stored item in a grid layout.
std::array<int, 4> grid_rect(const Scene& s, const Scene::InvLayout& L, const d2d::d2s::Item& it) {
    const auto info = s.rules.item_info.find(it.code);
    return { L.grid_x + it.column * L.box_w, L.grid_y + it.row * L.box_h,
             (info != s.rules.item_info.end() ? info->second.w : 1) * L.box_w,
             (info != s.rules.item_info.end() ? info->second.h : 1) * L.box_h };
}

// A left-side storage panel: the stash (panel 5) or the cube (panel 4).
void draw_storage(std::vector<std::uint8_t>& fb, const Scene& s, const std::vector<d2d::d2s::Item>& items,
                  const d2d::dc6::Sprite& art, const Scene::InvLayout& L, int panel,
                  int mx, int my, int clvl) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    if (art.frames_per_direction() >= 4) {
        const auto& f0 = art.frame(0, 0);
        blit_sprite(fb, f0, pal, kCharPanelX, kCharPanelY);
        blit_sprite(fb, art.frame(0, 1), pal, kCharPanelX + int(f0.width), kCharPanelY);
        blit_sprite(fb, art.frame(0, 2), pal, kCharPanelX, kCharPanelY + int(f0.height));
        blit_sprite(fb, art.frame(0, 3), pal, kCharPanelX + int(f0.width), kCharPanelY + int(f0.height));
    }
    const d2d::d2s::Item* hover = nullptr;
    std::array<int, 4> hb{};
    for (const auto& it : items) {
        if (it.location != 0 || it.panel != panel) continue;
        const auto [x, y, w, h] = grid_rect(s, L, it);
        if (const auto* spr = s.item_sprite(it); spr && spr->frames_per_direction() > 0) {
            const auto& f = spr->frame(0, 0);
            blit_sprite(fb, f, pal, x + (w - int(f.width)) / 2, y + (h - int(f.height)) / 2);
        }
        if (mx >= x && mx < x + w && my >= y && my < y + h) { hover = &it; hb = { x, y, w, h }; }
    }
    if (hover) draw_hover_text(fb, s, item_lines(s, *hover, clvl), hb[0], hb[0] + hb[2], hb[1] + hb[3], hb[1]);
}

// The equipped belt's belts.txt index (armor.txt `belt`), 2 ("default":
// one row) without one — as the popup code picks it (0x49906b).
int belt_index(const Scene& s, const std::vector<d2d::d2s::Item>& items) {
    for (const auto& it : items)
        if (it.location == 1 && it.slot == 8)
            if (const auto i = s.rules.item_info.find(it.code); i != s.rules.item_info.end() && i->second.belt >= 0
                && i->second.belt < 7)
                return i->second.belt;
    return 2;
}

// The belt: items in location 2 keep their slot (0..15, 4 per row) in the
// column field and sit centred in the belt's belts.txt boxes. Row 1 is the
// HUD strip; with the popup open (0x499136) each further row gets a
// ctrlpnl_popbelt frame 0, bottom-anchored at x W/2+21, bottom H-41-32i,
// and its items. Hovering an item shows its hover text.
// ponytail: no slot hotkey numbers.
void draw_belt(std::vector<std::uint8_t>& fb, const Scene& s, const std::vector<d2d::d2s::Item>& items,
               int mx, int my, int clvl, bool popup) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const auto& B = s.belts[std::size_t(belt_index(s, items))];
    const int rows = B.boxes / 4;
    if (popup && s.popbelt.frames_per_direction() > 0) {
        const auto& f = s.popbelt.frame(0, 0);
        for (int i = 0; i + 1 < rows; ++i)
            blit_sprite(fb, f, pal, int(kW) / 2 + 21, int(kH) - 41 - 32 * i - int(f.height) + 1);
    }
    const d2d::d2s::Item* hover = nullptr;
    std::array<int, 4> hb{};
    for (const auto& it : items) {
        if (it.location != 2 || it.column < 0 || it.column >= B.boxes || (!popup && it.column > 3)) continue;
        const auto& b = B.box[std::size_t(it.column)];
        if (b[1] <= b[0]) continue;
        if (const auto* spr = s.item_sprite(it); spr && spr->frames_per_direction() > 0) {
            const auto& f = spr->frame(0, 0);
            blit_sprite(fb, f, pal, b[0] + (b[1] - b[0] + 1 - int(f.width)) / 2,
                        b[2] + (b[3] - b[2] + 1 - int(f.height)) / 2);
        }
        if (mx >= b[0] && mx <= b[1] && my >= b[2] && my <= b[3]) { hover = &it; hb = b; }
    }
    if (hover) draw_hover_text(fb, s, item_lines(s, *hover, clvl), hb[0], hb[1] + 1, hb[3] + 1, hb[2]);
}

// The automap (UI\automap.cpp). Each revealed tile adds one cell
// (FUN_00457cf0): its cel from AutoMap.txt (FUN_0061fff0) at the tile's
// world pixel position ((x - y) * 80, (x + y) * 40) / 10, lower walls
// (orientation > 15) 24 further down. Drawn (FUN_00459700/FUN_00459440)
// at cell - scroll, scroll = player's world pixels / 10 - screen / 2 +
// (40, 15), with DC6's bottom-left anchoring.
// ponytail: reveals tiles within 12 of the player (D2 reveals by room),
// cel picked by a tile hash rather than the game's RNG, no fade near the
// centre, no player/NPC marks.
// One automap per Levels.txt Layer: the town and the act 1 wilderness
// share layer 0, so the map shows them together; cells sit in act
// coordinates.
struct Automap {
    struct Cell { int cel, x, y; };
    std::vector<Cell> cells;
    std::unordered_map<int, std::vector<std::uint8_t>> revealed;   // per level, per DS1 tile
    bool open = false;
};

int automap_cel(const Scene& s, const Level& L, int orientation, int main, int sub, std::uint32_t hash) {
    for (const auto& r : s.automap_rules) {
        if (r.level_type != L.type || r.orientation != orientation) continue;
        if (r.main >= 0 && r.main != main) continue;
        if (r.sub0 >= 0 && (sub < r.sub0 || sub > r.sub1)) continue;
        return r.cels[hash % r.cels.size()];
    }
    return -1;
}

void automap_reveal(const Scene& s, const Level& level, Automap& am, float px, float py) {
    const auto& m = level.ds1;
    const int w = int(m.width()), h = int(m.height());
    if (w == 0) return;
    auto& seen = am.revealed[level.id];
    if (seen.size() != std::size_t(w * h)) seen.assign(std::size_t(w * h), 0);
    const int cx = int(std::floor(px)), cy = int(std::floor(py)), R = 12;
    for (int ty = std::max(0, cy - R); ty < std::min(h, cy + R); ++ty)
        for (int tx = std::max(0, cx - R); tx < std::min(w, cx + R); ++tx) {
            auto& done = seen[std::size_t(ty * w + tx)];
            if (done) continue;
            done = 1;
            const int wx = tx + level.world_x, wy = ty + level.world_y;     // act tiles
            const std::uint32_t hash = std::uint32_t(wx * 73856093) ^ std::uint32_t(wy * 19349663);
            const int ax = (wx - wy) * 80 / 10, ay = (wx + wy) * 40 / 10;
            for (const auto& L : m.floors()) {
                const auto& t = L.cells[std::size_t(ty * w + tx)];
                if (t.hidden || !(t.prop1 & 2)) continue;
                if (const int c = automap_cel(s, level, 0, t.style, t.sequence, hash); c >= 0) am.cells.push_back({ c, ax, ay });
            }
            for (const auto& L : m.walls()) {
                const auto& t = L.cells[std::size_t(ty * w + tx)];
                if (t.hidden || t.wall_type == 0) continue;
                if (const int c = automap_cel(s, level, t.wall_type, t.style, t.sequence, hash); c >= 0)
                    am.cells.push_back({ c, ax, ay + (t.wall_type > 15 ? 24 : 0) });
            }
        }
}

void draw_automap(std::vector<std::uint8_t>& fb, const Scene& s, const Automap& am, float px, float py) {
    if (!am.open || s.automap_cels.frames_per_direction() == 0) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int scroll_x = int(std::lround((px - py) * 80 / 10)) - int(kW) / 2 + 40;
    const int scroll_y = int(std::lround((px + py) * 40 / 10)) - int(kH) / 2 + 15;
    for (const auto& c : am.cells) {
        if (c.cel < 0 || std::uint32_t(c.cel) >= s.automap_cels.frames_per_direction()) continue;
        const auto& f = s.automap_cels.frame(0, std::uint32_t(c.cel));
        const int x = c.x - scroll_x, y = c.y - scroll_y;
        if (x < -32 || x > int(kW) + 32 || y < -64 || y > int(kH) + 64) continue;
        blit_sprite(fb, f, pal, x, y - int(f.height) + 1);
    }
    // Your own mark (FUN_0045a860 -> FUN_0045a7f0): the 13-point shape at
    // 0x6d6638 doubled, at (unit px / div - scroll + 8, py / div - scroll
    // - 8), in the palette colour nearest FUN_004fb180(0, 0, 0xff) — the
    // palette is BGR, so red (party green, other players blue).
    static constexpr int kMark[13][2] = { {0,-1},{2,-2},{4,-1},{2,0},{4,1},{2,2},{0,1},{-2,2},{-4,1},{-2,0},{-4,-1},{-2,-2},{0,-1} };
    std::uint8_t mr = 255, mg = 0, mb = 0;
    {
        int best = 1 << 30;
        for (std::size_t i = 0; i < 256 && i < pal.entries().size(); ++i) {
            const auto c = pal[std::uint8_t(i)];
            const int d = (c.r - 255) * (c.r - 255) + c.g * c.g + c.b * c.b;
            if (d < best) { best = d; mr = c.r; mg = c.g; mb = c.b; }
        }
    }
    const int ux = int(std::lround((px - py) * 80 / 10)) - scroll_x + 8;
    const int uy = int(std::lround((px + py) * 40 / 10)) - scroll_y - 8;
    auto plot = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= int(kW) || y >= int(kH)) return;
        auto* p = &fb[(std::size_t(y) * kW + std::size_t(x)) * 4];
        p[0] = mr; p[1] = mg; p[2] = mb;
    };
    for (int i = 0; i + 1 < 13; ++i) {                   // Bresenham, like a D2GFX line
        int x0 = ux + kMark[i][0] * 2, y0 = uy + kMark[i][1] * 2;
        const int x1 = ux + kMark[i + 1][0] * 2, y1 = uy + kMark[i + 1][1] * 2;
        const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        for (int err = dx + dy;;) {
            plot(x0, y0);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

// The waypoint panel (FUN_0049c9c0; hit tests FUN_0049c490/0049c510),
// in the left-panel spot at 800x600 (+80, +60 on game.exe's numbers):
// - waygatebackground 2x2; act tabs at bottom 94, x 80 + {3, 67, 129,
//   191, 253} (expansion, 64 wide hits) or {3, 81, 159, 237} (80 wide),
//   frame 2i active / 2i+1 other, drawn only once the act is open
//   (quest 7, 15, 23, 26 done: the previous act's end);
// - up to 9 rows (0x7224e8, 6 ints each): icon at x 97, bottom 149 +
//   ~36i (frame 0 the level you're in, 3/4 activated / hovered, none
//   when not activated), name (Levels.txt LevelName) in font16 at x 160,
//   baseline 144 + 35i: grey not activated, blue hovered or current;
//   hover rows are activated ones, x 97..377, 30 tall from 120 + ~36i;
// - title centred at 240, baseline 108: "Choose your destination"
//   (0xf96) or "No Other Waypoints Activated" (0xf97);
// - cancel: buysellbtn frame 10/11 at 353, bottom 477 (hit 353..389,
//   447..481), "Cancel" (0x1022) on hover.
struct WaypointUI {
    bool open = false;
    int tab = 0, hover = -1;
    bool cancel_down = false;
};
constexpr int kWpIconBottom[9] = { 89, 125, 161, 197, 234, 270, 306, 342, 378 };
constexpr int kWpTextBase[9]   = { 84, 119, 154, 189, 224, 259, 294, 329, 364 };
constexpr int kWpHitTop[9]     = { 60, 96, 132, 168, 205, 241, 277, 313, 349 };

bool waypoint_act_open(const d2d::d2s::Header& h, int act) {
    static constexpr int kQuest[5] = { -1, 7, 15, 23, 26 };
    return act == 0 || h.quest_flag(h.active_difficulty(), kQuest[act], 0);
}

// Row under the cursor (activated waypoints only), or -1.
int waypoint_row_at(const Scene& s, const WaypointUI& ui, const d2d::d2s::Header& h, int mx, int my) {
    const auto& rows = s.waypoint_levels[std::size_t(ui.tab)];
    for (std::size_t i = 0; i < rows.size() && i < 9; ++i) {
        if (!h.waypoint(h.active_difficulty(), rows[i].wp)) continue;
        const int x = kCharPanelX + 17, y = 60 + kWpHitTop[i];
        if (mx > x && mx < x + 280 && my > y && my < y + 30) return int(i);
    }
    return -1;
}

// Tab under the cursor (open acts only), or -1.
int waypoint_tab_at(const d2d::d2s::Header& h, bool expansion, int mx, int my) {
    const int x = mx - kCharPanelX, y = my - 60;
    if (y > 30 || x < 0 || x > 320) return -1;
    const int t = std::min(x / (expansion ? 64 : 80), expansion ? 4 : 3);
    return waypoint_act_open(h, t) ? t : -1;
}

void draw_waypoints(std::vector<std::uint8_t>& fb, const Scene& s, const WaypointUI& ui,
                    const d2d::d2s::Header& h, bool expansion, int current_level, int mx, int my) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    if (s.wp_bg.frames_per_direction() >= 4) {
        const auto& f0 = s.wp_bg.frame(0, 0);
        blit_sprite(fb, f0, pal, kCharPanelX, kCharPanelY);
        blit_sprite(fb, s.wp_bg.frame(0, 1), pal, kCharPanelX + int(f0.width), kCharPanelY);
        blit_sprite(fb, s.wp_bg.frame(0, 2), pal, kCharPanelX, kCharPanelY + int(f0.height));
        blit_sprite(fb, s.wp_bg.frame(0, 3), pal, kCharPanelX + int(f0.width), kCharPanelY + int(f0.height));
    }
    static constexpr int kTabX[2][5] = { { 3, 81, 159, 237, 0 }, { 3, 67, 129, 191, 253 } };
    const auto& tabs = s.wp_tabs[expansion ? 1 : 0];
    for (int i = 0; i < (expansion ? 5 : 4); ++i) {
        if (i != ui.tab && !waypoint_act_open(h, i)) continue;
        const auto fi = std::uint32_t(i * 2 + (i == ui.tab ? 0 : 1));
        if (fi >= tabs.frames_per_direction()) continue;
        const auto& f = tabs.frame(0, fi);
        blit_sprite(fb, f, pal, kCharPanelX + kTabX[expansion ? 1 : 0][i], 60 + 34 - int(f.height) + 1);
    }
    const int cell = s.font.sheet().frames_per_direction() > 0 ? int(s.font.sheet().frame(0, 0).height) : 16;
    auto text = [&](int x, int baseline, const std::string& t, std::array<std::uint8_t, 3> c) {
        s.font.draw_tinted(fb, kW, kH, pal, x, baseline - cell + 1, t, c[0], c[1], c[2]);
    };
    const int diff = h.active_difficulty();
    const auto& rows = s.waypoint_levels[std::size_t(ui.tab)];
    const int hover = waypoint_row_at(s, ui, h, mx, my);
    bool others = false;
    for (const auto& a : s.waypoint_levels)
        for (const auto& r : a) others |= r.level != current_level && h.waypoint(diff, r.wp);
    for (std::size_t i = 0; i < rows.size() && i < 9; ++i) {
        const bool active = h.waypoint(diff, rows[i].wp), here = rows[i].level == current_level;
        const int fi = here ? 0 : active ? 3 + (hover == int(i)) : -1;
        if (fi >= 0 && std::uint32_t(fi) < s.wp_icons.frames_per_direction()) {
            const auto& f = s.wp_icons.frame(0, std::uint32_t(fi));
            blit_sprite(fb, f, pal, kCharPanelX + 17, 60 + kWpIconBottom[i] - int(f.height) + 1);
        }
        const auto name = lookup_string(s, std::string_view(rows[i].name));
        text(kCharPanelX + 80, 60 + kWpTextBase[i], name ? u16_to_latin1(*name) : rows[i].name,
             !active ? kTxtGrey : here || hover == int(i) ? kTxtBlue : kTxtWhite);
    }
    const auto title = string_id(s, others ? 0xf96 : 0xf97);
    text(kCharPanelX + 160 - s.font.measure(title) / 2, 108, title, kTxtWhite);
    if (std::uint32_t(11) < s.store_buttons.frames_per_direction()) {
        const auto& f = s.store_buttons.frame(0, ui.cancel_down ? 11 : 10);
        blit_sprite(fb, f, pal, kCharPanelX + 0x111, 477 - int(f.height) + 1);
    }
    if (mx - (kCharPanelX + 0x111) >= 0 && mx - (kCharPanelX + 0x111) < 0x24 && my - 0x183 - 60 >= 0 && my - 0x183 - 60 < 0x22) {
        const auto c = string_id(s, 0x1022);
        const int w = s.font.measure(c);
        draw_hover_text(fb, s, { { c, kTxtWhite } }, kCharPanelX + 0x126 - w / 2, kCharPanelX + 0x126 + w / 2, 0x183 + 60, 0x172 + 60);
    }
}

}  // namespace
