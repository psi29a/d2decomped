// SPDX-License-Identifier: GPL-3.0-or-later
#include "common.hpp"
#include "input/touch/input.hpp"
#include "input/touch/trade.hpp"
#include "panels/store.hpp"
#include "scene.hpp"
#include "session.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <d2s_items.hpp>
#include <font.hpp>
#include <item_gen.hpp>
#include <protocol.hpp>
#include <string_view>
#include <vector>
namespace d2d::client {
const d2d::d2s::Item* ClientSession::touch_bag_item() const {
    const auto found = std::ranges::find(character.items, touch_item, &d2d::d2s::Item::id);
    if (found == character.items.end() || found->location != d2d::d2s::item_location::kStored || found->panel != d2d::d2s::item_panel::kInventory) return nullptr;
    return &*found;
}
bool ClientSession::touch_can_sell(const d2d::d2s::Item* item) const {
    return scene && item && inv_open && (!held || (item == &*held && item->id == touch_item)) && store.npc >= 0 && !store.gamble && store.mode != 1 && store.mode != 3
        && identify_with < 0 && !use_item.open() && d2d::rules::sellable(scene->rules, *item);
}
touch::Box ClientSession::touch_trade_drop() const {
    if (!scene || touch_item_finger == -1 || !touch_item_drag) return {};
    if (!held && touch_stock.index(store) >= 0) {
        const auto& layout = touch_inv_layout();
        return { float(layout.grid_x), float(layout.grid_y), float(layout.cols * layout.box_w), float(layout.rows * layout.box_h) };
    }
    if (touch_sell_vendor == store.npc && touch_sell_tab == store.tab && touch_can_sell(held && held->id == touch_item ? &*held : touch_bag_item())) {
        const auto layout = store_grid(touch_store_panel_x());
        return { float(layout.grid_x), float(layout.grid_y), float(layout.cols * layout.box_w), float(layout.rows * layout.box_h) };
    }
    return {};
}
touch::BagDrop ClientSession::touch_bag_drop(float x, float y) const {
    if (!scene) return touch::BagDrop::None;
    const auto layout = touch_inv_layout();
    const touch::Box inventory{ float(layout.grid_x), float(layout.grid_y),
        float(layout.cols * layout.box_w), float(layout.rows * layout.box_h) };
    return touch::bag_drop(inventory, touch_trade_drop(), x, y);
}
void ClientSession::draw_touch_trade_hint(std::vector<std::uint8_t>& framebuffer) const {
    auto box = touch_trade_drop();
    box.x = touch::to_viewport_x(box.x);
    const int screen_width = fb_width(framebuffer);
    const bool dropping = box.width > 0.f && box.height > 0.f;
    const bool inside = dropping && box.contains(touch_pointer.x, touch_pointer.y);
    if (dropping) {
        const int left = std::max(0, int(box.x)), top = std::max(0, int(box.y));
        const int right = std::min(screen_width, int(box.x + box.width));
        const int bottom = std::min(int(kScreenHeight), int(box.y + box.height));
        for (int pixel_y = top; pixel_y < bottom; ++pixel_y)
            for (int pixel_x = left; pixel_x < right; ++pixel_x) {
                const int stroke = inside ? 4 : 2;
                if (pixel_x >= left + stroke && pixel_x < right - stroke && pixel_y >= top + stroke && pixel_y < bottom - stroke) continue;
                auto* pixel = &framebuffer[(std::size_t(pixel_y) * std::size_t(screen_width) + std::size_t(pixel_x)) * 4];
                pixel[0] = inside ? 70 : 170; pixel[1] = inside ? 200 : 150; pixel[2] = 60;
            }
    }
    const auto& font = scene->font_small;
    const auto& palette = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
    const std::string_view hint = identify_with >= 0 ? "Tap an unidentified item . Back cancels"
        : dropping ? (inside ? (touch_stock.tab >= 0 ? "Release to buy" : "Release to sell") : "Drag into the highlighted grid")
        : std::string_view(touch_item_hint);
    font.draw(framebuffer, screen_width, kScreenHeight, palette, screen_width / 2 - font.measure(hint) / 2, 500, hint);
}
void ClientSession::close_touch_item() {
    touch_item_finger = -1; touch_item = touch_sell_vendor = touch_sell_tab = -1; touch_stock = {};
    touch_item_drag = touch_sold = touch_hold_edge = false;
    touch_hold.clear(); touch_pointer = {}; touch_item_hint.clear();
}
void ClientSession::cancel_touch_item() {
    if (touch_item_finger != -1) touch_cancelled_item_finger = touch_item_finger;
    else if (touch_sheet.trade_finger != -1) touch_cancelled_item_finger = touch_sheet.trade_finger;
    touch_sheet = {}; close_touch_item();
}
bool ClientSession::touch_trade(touch::ItemAction action) {
    if (action == touch::ItemAction::Buy) {
        if (held) return false;
        const int index = touch_stock.index(store);
        if (index < 0) return false;
        buy_stock(index);
        return true;
    }
    if (action == touch::ItemAction::Sell && touch_sell_vendor == store.npc && touch_sell_tab == store.tab && touch_can_sell(held && held->id == touch_item ? &*held : touch_bag_item())) {
        send(cmd::Sell{ touch_item });
        return true;
    }
    return false;
}
} // namespace d2d::client
