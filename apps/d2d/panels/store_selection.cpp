// SPDX-License-Identifier: GPL-3.0-or-later
#include "panels/store_selection.hpp"
#include <algorithm>
#include <cstddef>
#include <d2s_items.hpp>
#include <vendor.hpp>
namespace d2d::client {
StoreSelection StoreSelection::capture(const d2d::rules::Store& store, int index) {
    if (store.npc < 0 || store.tab < 0 || store.tab >= 4 || index < 0 || std::size_t(index) >= store.tabs[std::size_t(store.tab)].size()) return {};
    return { store.npc, store.tab, store.tabs[std::size_t(store.tab)][std::size_t(index)].id, store.gamble };
}
int StoreSelection::index(const d2d::rules::Store& store) const {
    if (npc < 0 || npc != store.npc || tab < 0 || tab >= 4 || tab != store.tab || gamble != store.gamble || id == -1) return -1;
    const auto& stock = store.tabs[std::size_t(tab)];
    const auto found = std::ranges::find(stock, id, &d2d::d2s::Item::id);
    return found == stock.end() ? -1 : int(found - stock.begin());
}
} // namespace d2d::client
