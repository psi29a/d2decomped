// SPDX-License-Identifier: GPL-3.0-or-later
// Inventory prompts and dispatch consult the same availability decision.
#pragma once

#include <string_view>

namespace d2d::client {
enum class ItemUseKind { None, Drink, Identify, Portal, Cube };
struct ItemUse { ItemUseKind kind = ItemUseKind::None; std::string_view label; };
/**
 * Resolve an inventory item action and its availability message from the supplied item/player
 * state. Both prompts and dispatch use this result; unavailable tomes do not produce an action.
 */
[[nodiscard]] inline ItemUse item_use_action(std::string_view code, int quantity, bool potion, bool in_town, bool dead, bool unidentified) {
    if (dead) return { ItemUseKind::None, "Unavailable while dead" };
    if ((code == "ibk" || code == "tbk") && quantity <= 0) return { ItemUseKind::None, "Tome empty" };
    if (code == "ibk" || code == "isc") return unidentified ? ItemUse{ ItemUseKind::Identify, "Identify" } : ItemUse{ ItemUseKind::None, "No unidentified items" };
    if (code == "tbk" || code == "tsc") return in_town ? ItemUse{ ItemUseKind::None, "Portal unavailable in town" } : ItemUse{ ItemUseKind::Portal, "Open portal" };
    if (code == "box") return { ItemUseKind::Cube, "Open cube" };
    if (potion) return { ItemUseKind::Drink, "Drink" };
    return {};
}
} // namespace d2d::client
