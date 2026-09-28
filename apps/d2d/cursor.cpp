// Definitions for cursor.hpp: the item in hand and panel clicks.
#include "cursor.hpp"

#include "common.hpp"
#include "panels.hpp"
#include "scene.hpp"

#include <d2s_items.hpp>
#include <rules.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace d2d::client {

CursorClick item_cursor_command(const Scene& scene, const std::vector<d2d::d2s::Item>& items, const std::optional<d2d::d2s::Item>& held,
                                int save_cls, const OpenPanels& open, int mouse_x, int mouse_y) {
    const auto& tables = scene.rules;
    auto inside = [&](int x, int y, int width, int height) { return mouse_x >= x && mouse_x < x + width && mouse_y >= y && mouse_y < y + height; };
    // Grids: the inventory (panel 1), and the stash (5) or cube (4) on the left.
    struct Grid { const Scene::InvLayout* layout; int panel; };
    std::vector<Grid> grids;
    const auto& inv = scene.inv_layout[std::size_t(save_cls)];
    if (open.inv) grids.push_back({ &inv, 1 });
    if (open.cube) grids.push_back({ &scene.cube_layout, 4 });
    else if (open.stash) grids.push_back({ &scene.stash_layout[open.expansion ? 1 : 0], 5 });
    for (const auto& [layout, panel] : grids) {
        if (!inside(layout->grid_x, layout->grid_y, layout->cols * layout->box_w, layout->rows * layout->box_h)) continue;
        if (held) {
            // The held item is drawn centred on the cursor: its top-left
            // cell is the one under the cursor, shifted back half its size.
            const auto [width, height] = d2d::rules::item_size(tables, held->code);
            const int col = int(std::floor((float(mouse_x - layout->grid_x) - float((width - 1) * layout->box_w) / 2.f) / float(layout->box_w)));
            const int row = int(std::floor((float(mouse_y - layout->grid_y) - float((height - 1) * layout->box_h) / 2.f) / float(layout->box_h)));
            return { true, cmd::ToGrid{ panel, col, row } };
        }
        for (const auto& item : items) {
            if (item.location != 0 || item.panel != panel) continue;
            const auto rect = grid_rect(scene, *layout, item);
            if (inside(rect[0], rect[1], rect[2], rect[3])) return { true, cmd::ToCursor{ item.id } };
        }
        return { true, {} };
    }
    if (open.inv)
        for (int slot = 1; slot <= 10; ++slot) {
            const auto& rect = inv.slots[std::size_t(slot)];
            if (rect[2] <= 0 || !inside(rect[0], rect[1], rect[2], rect[3])) continue;
            if (held) return { true, cmd::ToBody{ slot } };
            for (const auto& item : items)
                if (item.location == 1 && item.slot == slot) return { true, cmd::ToCursor{ item.id } };
            return { true, {} };
        }
    // Belt boxes: row 1 on the HUD strip, the rest with the popup open.
    const auto& belt = scene.belts[std::size_t(belt_index(scene, items))];
    for (int box = 0; box < belt.boxes && box < int(belt.box.size()); ++box) {
        if (box > 3 && !open.belt_popup) break;
        const auto& rect = belt.box[std::size_t(box)];
        if (rect[1] <= rect[0] || mouse_x < rect[0] || mouse_x > rect[1] || mouse_y < rect[2] || mouse_y > rect[3]) continue;
        if (held) return { true, cmd::ToBelt{ box } };
        for (const auto& item : items)
            if (item.location == 2 && item.column == box) return { true, cmd::ToCursor{ item.id } };
        return {};                                         // empty box: the strip toggles the popup
    }
    return {};
}

void draw_held(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const d2d::d2s::Item& item, int mouse_x, int mouse_y) {
    const auto* spr = scene.item_sprite(item);
    if (!spr || spr->frames_per_direction() == 0) return;
    const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
    const auto& frame = spr->frame(0, 0);
    blit_sprite(framebuffer, frame, scene.item_pal(item, pal), mouse_x - int(frame.width) / 2, mouse_y - int(frame.height) / 2);
}

}  // namespace d2d::client
