// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for inventory.hpp.
#include "inventory.hpp"

#include "gamedata.hpp"
#include "level.hpp"

#include <d2s_items.hpp>
#include <drops.hpp>
#include <rules.hpp>
#include <vendor.hpp>

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace d2d::game {

Store open_store(const GameData& game_data, const Level& level, int npc, int act, int difficulty, int clvl, bool expansion,
                        d2d::rules::Rng& proxy, d2d::rules::Rng& game, std::vector<bool>* found_uniques) {
    const auto& vendor = level.npcs[std::size_t(npc)];
    const auto& tables = game_data.rules;
    Store store;
    store.npc = npc;
    store.npc_id = vendor.id;
    store.hc_idx = vendor.hc_idx;
    store.vendor = d2d::rules::vendor_index(vendor.hc_idx);
    if (store.vendor < 0) return store;
    for (const auto& item : tables.vendor_items[std::size_t(store.vendor)]) if (item.perm) store.perm.push_back(item.code);
    d2d::rules::store_fill(tables, store.vendor, d2d::rules::store_level(clvl, act, difficulty), expansion, proxy, [&](const d2d::rules::StoreAsk& ask) {
        auto made = d2d::rules::make_store_item(tables, ask, difficulty, clvl, expansion, proxy, game, found_uniques);
        if (!made) return false;
        const auto info = tables.item_info.find(made->code);
        const auto type = info != tables.item_info.end() ? tables.types.find(info->second.type) : tables.types.end();
        const int page = type != tables.types.end() ? type->second.store_page : -1;
        return page >= 0 && d2d::rules::store_place(tables, store, page, std::move(*made));
    });
    store.first_tab();
    return store;
}

}  // namespace d2d::game
