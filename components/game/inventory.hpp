// SPDX-License-Identifier: GPL-3.0-or-later
// The character's items as the rules move them: what an equip check
// sees, the belt in use, a held item put away, a vendor's store.
#pragma once

#include "character.hpp"
#include "gamedata.hpp"

#include <d2s_items.hpp>
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
    if (const auto [x, y] = d2d::rules::free_spot(game_data.rules, inv, 10, 4, width, height); x >= 0) {
        d2d::rules::put_in_grid(game_data.rules, items, held, d2d::d2s::item_panel::kInventory, 10, 4, x, y);
        return;
    }
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

// The store for world NPC npc (stock rolled from rng).
inline Store open_store(const GameData& game_data, const Level& level, int npc, d2d::rules::Rng& rng) {
    const auto& vendor = level.npcs[std::size_t(npc)];
    Store store = d2d::rules::open_store(game_data.rules, vendor.hc_idx, vendor.id, rng);
    store.npc = npc;
    return store;
}

}  // namespace d2d::game
