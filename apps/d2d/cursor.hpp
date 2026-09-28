// The item cursor: a left click on an open panel's grid, a body slot or a
// belt box picks up the item there, or puts the held one down.
#pragma once

#include "protocol.hpp"
#include "store.hpp"

namespace d2d::app {

// Which panels are open, for hit-testing.
struct OpenPanels { bool inv = false, stash = false, cube = false, belt_popup = false, expansion = true; };

// The strength, dexterity and level an equip check sees: base stats plus
// what the worn gear adds (stats 0 strength, 2 dexterity).
// ponytail: worn items' own props only (no sockets, sets, charms).
d2d::rules::Wearer wearer(int save_cls, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Stats& st) {
    d2d::rules::Wearer w{ save_cls, int(st.get(d2d::d2s::kStr)), int(st.get(d2d::d2s::kDex)),
                          int(st.get(d2d::d2s::kLevel)) };
    for (const auto& it : items)
        if (it.location == 1)
            for (const auto& p : it.props) {
                if (p.stat == 0) w.str += p.value;
                if (p.stat == 2) w.dex += p.value;
            }
    return w;
}

// A left click at (mx, my) with the panels in `open`: picks up or puts
// down. Returns whether the click landed on an item spot (so it doesn't
// also walk or toggle the belt).
// A click's item action (the client's side): the command for the World
// (protocol.hpp), and whether the click was the cursor's at all.
struct CursorClick { bool consumed = false; std::optional<Command> cmd; };
CursorClick item_cursor_command(const Scene& s, const std::vector<d2d::d2s::Item>& items, const std::optional<d2d::d2s::Item>& held,
                                int save_cls, const OpenPanels& open, int mx, int my) {
    const auto& t = s.rules;
    auto inside = [&](int x, int y, int w, int h) { return mx >= x && mx < x + w && my >= y && my < y + h; };
    // Grids: the inventory (panel 1), and the stash (5) or cube (4) on the left.
    struct Grid { const Scene::InvLayout* L; int panel; };
    std::vector<Grid> grids;
    const auto& inv = s.inv_layout[std::size_t(save_cls)];
    if (open.inv) grids.push_back({ &inv, 1 });
    if (open.cube) grids.push_back({ &s.cube_layout, 4 });
    else if (open.stash) grids.push_back({ &s.stash_layout[open.expansion ? 1 : 0], 5 });
    for (const auto& [L, panel] : grids) {
        if (!inside(L->grid_x, L->grid_y, L->cols * L->box_w, L->rows * L->box_h)) continue;
        if (held) {
            // The held item is drawn centred on the cursor: its top-left
            // cell is the one under the cursor, shifted back half its size.
            const auto [w, h] = d2d::rules::item_size(t, held->code);
            const int col = int(std::floor((float(mx - L->grid_x) - float((w - 1) * L->box_w) / 2.f) / float(L->box_w)));
            const int row = int(std::floor((float(my - L->grid_y) - float((h - 1) * L->box_h) / 2.f) / float(L->box_h)));
            return { true, cmd::ToGrid{ panel, col, row } };
        }
        for (const auto& it : items) {
            if (it.location != 0 || it.panel != panel) continue;
            const auto r = grid_rect(s, *L, it);
            if (inside(r[0], r[1], r[2], r[3])) return { true, cmd::ToCursor{ it.id } };
        }
        return { true, {} };
    }
    if (open.inv)
        for (int slot = 1; slot <= 10; ++slot) {
            const auto& r = inv.slots[std::size_t(slot)];
            if (r[2] <= 0 || !inside(r[0], r[1], r[2], r[3])) continue;
            if (held) return { true, cmd::ToBody{ slot } };
            for (const auto& it : items)
                if (it.location == 1 && it.slot == slot) return { true, cmd::ToCursor{ it.id } };
            return { true, {} };
        }
    // Belt boxes: row 1 on the HUD strip, the rest with the popup open.
    const auto& B = s.belts[std::size_t(belt_index(s, items))];
    for (int b = 0; b < B.boxes && b < int(B.box.size()); ++b) {
        if (b > 3 && !open.belt_popup) break;
        const auto& r = B.box[std::size_t(b)];
        if (r[1] <= r[0] || mx < r[0] || mx > r[1] || my < r[2] || my > r[3]) continue;
        if (held) return { true, cmd::ToBelt{ b } };
        for (const auto& it : items)
            if (it.location == 2 && it.column == b) return { true, cmd::ToCursor{ it.id } };
        return {};                                         // empty box: the strip toggles the popup
    }
    return {};
}

// Puts a held item back when the game is left: the first free inventory
// spot, else it's kept as d2s location 4 (on the cursor) so it isn't lost.
void stow_held(const GameData& s, std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held) {
    if (!held) return;
    std::vector<const d2d::d2s::Item*> inv;
    for (const auto& it : items) if (it.location == 0 && it.panel == 1) inv.push_back(&it);
    const auto [w, h] = d2d::rules::item_size(s.rules, held->code);
    if (const auto [x, y] = d2d::rules::free_spot(s.rules, inv, 10, 4, w, h); x >= 0) {
        d2d::rules::put_in_grid(s.rules, items, held, 1, 10, 4, x, y);
        return;
    }
    held->location = 4;
    items.push_back(std::move(*held));
    held.reset();
}

// The held item, centred on the cursor (D2 hides the hand while holding).
void draw_held(std::vector<std::uint8_t>& fb, const Scene& s, const d2d::d2s::Item& it, int mx, int my) {
    const auto* spr = s.item_sprite(it);
    if (!spr || spr->frames_per_direction() == 0) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const auto& f = spr->frame(0, 0);
    blit_sprite(fb, f, s.item_pal(it, pal), mx - int(f.width) / 2, my - int(f.height) / 2);
}

}  // namespace d2d::app
