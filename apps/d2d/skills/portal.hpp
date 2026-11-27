// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <d2s_items.hpp>

namespace d2d::client::skillpad {
// Prefer a charged tome, then a loose scroll; empty tomes remain assignable.
template <class Items>
int portal_item_id(const Items& items, bool charged = true) {
    int scroll = -1;
    for (const auto& item : items) {
        if (item.location != d2d::d2s::item_location::kStored || item.panel != d2d::d2s::item_panel::kInventory) continue;
        if (item.code == "tbk" && (!charged || item.quantity > 0)) return item.id;
        if (item.code == "tsc") scroll = item.id;
    }
    return scroll;
}
} // namespace d2d::client::skillpad
