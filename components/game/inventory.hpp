// SPDX-License-Identifier: GPL-3.0-or-later
// The character's items as the rules move them: what an equip check
// sees, the belt in use, a held item put away, a vendor's store.
#pragma once

#include "character.hpp"
#include "gamedata.hpp"

#include <d2s_items.hpp>
#include <drops.hpp>
#include <rules.hpp>

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace d2d::game {

using d2d::rules::Store;

// The strength, dexterity and level an equip check sees: base stats plus
// what the gear adds (gear_props; stats 0 strength, 2 dexterity).
inline d2d::rules::Wearer wearer(const GameData& game_data, int save_cls, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Stats& stats) {
    d2d::rules::Wearer wearer{ save_cls, int(stats.get(d2d::d2s::kStr)), int(stats.get(d2d::d2s::kDex)),
                          int(stats.get(d2d::d2s::kLevel)) };
    for (const auto& prop : gear_props(game_data, items)) {
        if (prop.stat == d2d::d2s::kStr) wearer.str += prop.value;
        if (prop.stat == d2d::d2s::kDex) wearer.dex += prop.value;
    }
    return wearer;
}

// Puts a held item back when the game is left: the first free inventory
// spot, else it's kept as d2s location 4 (on the cursor) so it isn't lost.
inline void stow_held(const GameData& game_data, std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held) {
    if (!held) return;
    std::vector<const d2d::d2s::Item*> inv;
    for (const auto& item : items) if (item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory) inv.push_back(&item);
    const auto [width, height] = d2d::rules::item_size(game_data.rules, held->code);
    if (const auto [column, row] = d2d::rules::free_spot(game_data.rules, inv, 10, 4, width, height, true);
        column >= 0 && d2d::rules::put_in_grid(game_data.rules, items, held, d2d::d2s::item_panel::kInventory, 10, 4, column, row))
        return;
    held->location = d2d::d2s::item_location::kCursor;
    items.push_back(std::move(*held));
    held.reset();
}

// The equipped belt's belts.txt index (armor.txt `belt`), 2 ("default":
// one row) without one — as the popup code picks it (0x49906b).
inline int belt_index(const GameData& game_data, const std::vector<d2d::d2s::Item>& items) {
    for (const auto& item : items)
        if (item.location == d2d::d2s::item_location::kEquipped && item.slot == d2d::d2s::body_location::kBelt)
            if (const auto found = game_data.rules.item_info.find(item.code); found != game_data.rules.item_info.end() && found->second.belt >= 0
                && found->second.belt < 7)
                return found->second.belt;
    return 2;
}

// The store for world NPC npc as game.exe fills it (FUN_00576980,
// rules::store_fill): the asks on the proxy seed, each item made off the
// game seed (rules::make_store_item), put on its type's page (ItemTypes
// StorePage: FUN_0062e880; weapons spill onto page 2), one that has no page
// or no room failing (FUN_00560200's spot: rules::free_spot, a store's
// first fit). The permanent items stay buyable.
inline Store open_store(const GameData& game_data, const Level& level, int npc, int act, int difficulty, int clvl, bool expansion,
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
