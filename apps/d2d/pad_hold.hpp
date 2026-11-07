// SPDX-License-Identifier: GPL-3.0-or-later
// Which inventory hold A confirms. The shop never fills the belt or equips.
// A buy or repair mode, a gamble, an identify or an imbue does not sell.
// Outside a shop a potion fills the belt and a listed armor piece equips.
// Anything else is an ordinary click.
#pragma once

#include <rules.hpp>

#include <cstddef>
#include <string_view>

namespace d2d::client {

enum class PadHoldKind { None, Sell, Fill, Equip, FillOne };

struct PadHoldQuery {
    bool shop = false;
    bool gamble = false;
    bool identify = false;
    bool imbue = false;
    bool hand_empty = false;
    bool bag = false;
    bool sellable = false;
    bool potion = false;
    bool equippable = false;
    int store_mode = 0;               // 0 trade, 1 buy, 2 sell, 3 repair
};

// Healing, mana and rejuvenation share a belt column. Stamina (vps), antidote
// (yps) and thawing (wms) are potions too. The game also accepts any code in
// rules::Tables::potions.
[[nodiscard]] inline bool potion_code(std::string_view code) {
    if (code == "vps" || code == "yps" || code == "wms") return true;
    for (const std::string_view group : d2d::rules::kGroups)
        for (std::size_t offset = 0; code.size() == 3 && offset + 3 <= group.size(); offset += 3)
            if (group.substr(offset, 3) == code) return true;
    return false;
}

[[nodiscard]] inline PadHoldKind pad_hold_kind(const PadHoldQuery& query) {
    if (!query.hand_empty || !query.bag || query.identify || query.imbue) return PadHoldKind::None;
    if (query.shop) {
        if (query.gamble || query.store_mode == 1 || query.store_mode == 3 || !query.sellable) return PadHoldKind::None;
        return PadHoldKind::Sell;
    }
    if (query.potion) return PadHoldKind::Fill;
    if (query.equippable) return PadHoldKind::Equip;
    return PadHoldKind::None;
}

// EquipFromBag stays on this machine. A joined game must not classify, cue,
// or perform it: Town::send drops the command before the World applies it.
[[nodiscard]] inline bool pad_hold_can_equip(bool joined, bool equippable) { return !joined && equippable; }

// The drop packet is built from the cursor. A bag pickup has not filled that
// cursor yet, so a joined game must not pick up and drop in the same turn.
// An item already in hand still drops.
[[nodiscard]] inline bool pad_drops_from_bag(bool joined) { return !joined; }

// The line is a pad prompt. Mouse and keyboard play must not show it.
[[nodiscard]] inline bool pad_shows_hold_cue(bool pad_playing, bool can_potion, bool can_equip, bool can_sell) {
    return pad_playing && (can_potion || can_equip || can_sell);
}

// A is the full hold. Right trigger alone, on a potion, belts that one potion.
// Right trigger does not sell or equip.
[[nodiscard]] inline PadHoldKind pad_hold_button(const PadHoldQuery& query, bool south, bool trigger) {
    const PadHoldKind kind = pad_hold_kind(query);
    if (trigger && !south) return kind == PadHoldKind::Fill ? PadHoldKind::FillOne : PadHoldKind::None;
    if (!south) return PadHoldKind::None;
    return kind;
}

}  // namespace d2d::client
