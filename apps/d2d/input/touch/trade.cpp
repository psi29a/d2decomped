// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/touch/input.hpp"
#include "input/touch/trade.hpp"
#include <cstdint>
namespace d2d::client::touch {
BagDrop bag_drop(Box inventory, Box vendor, float x, float y) {
    if (inventory.contains(x, y)) return BagDrop::Inventory;
    if (vendor.contains(x, y)) return BagDrop::Sell;
    return BagDrop::None;
}

bool ItemSheet::trade_event(std::int64_t finger, Phase phase, float x, float y, ItemAction& action) {
    action = ItemAction::None;
    if (trade_finger == -1) {
        const auto pressed = hit(x, y);
        if (phase != Phase::Down || (pressed != ItemAction::Buy && pressed != ItemAction::Sell)) return false;
        trade_finger = finger; trade_action = pressed; trade_inside = true;
    }
    if (finger != trade_finger) return true;
    if (hit(x, y) != trade_action) trade_inside = false;
    if (phase == Phase::Up || phase == Phase::Cancel) {
        if (phase == Phase::Up && trade_inside) action = trade_action;
        trade_finger = -1; trade_action = ItemAction::None;
    }
    return true;
}
} // namespace d2d::client::touch
