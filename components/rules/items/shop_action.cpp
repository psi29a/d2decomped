// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of shop_action.hpp's functions; their notes are in the header.
#include "shop_action.hpp"

#include "rules.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace d2d::rules {

ShopKind shop_kind(int npc_class) {
    switch (npc_class) {
        case 0x93: case 0x94: case 0xb1: case 0xc7: case 0xca: case 0xfc: case 0xfe: case 0xff: case 0x195: case 0x200: case 0x201: case 0x202:
            return ShopKind::sell;
        case 0x9a: case 0xb2: case 0xfd: case 0x101: case 0x1ff: return ShopKind::repair;
        case 0xf4: case 0xf5: case 0xf6: case 0x109: case 0x208: return ShopKind::identify;
        default: return ShopKind::none;
    }
}

ShopResult shop_click(const ShopClick& request) {
    ShopResult out;
    if (!request.session || !request.npc_found) return out;
    const ShopKind kind = shop_kind(request.npc_class);
    if (request.repair_all) {
        if (kind != ShopKind::repair) return out;
        out.handled = true;
        if (!request.repair_all_flag) return out;     // the Repair All button sets the flag (0x7bcc48) around its call
        out.mode = 3; out.price_mode = 3; out.price_all = true; out.click = out.pending = out.ask = true;
        return out;
    }
    if (request.gold) return out;
    if (request.direct && !request.waited) return out;     // under 500 ms since the last ask (GetTickCount - 0x7c0df1 < 500)
    out.click = true;
    if (request.sell != 1) {                          // the vendor's stock: a buy, never with the cursor holding something
        if (request.hand) return out;
        out.mode = 1; out.price_mode = request.gamble ? 2 : 0;
    } else {
        switch (kind) {
            case ShopKind::none: return out;
            case ShopKind::identify: out.mode = 4; break;     // the flag test compares 0x10 with 1: never refuses
            case ShopKind::repair:
                if (request.repair_button) {
                    if (!request.wants_repair) return out;
                    out.mode = 3; out.price_mode = 3;
                    break;
                }
                [[fallthrough]];
            case ShopKind::sell:
                out.price_mode = 1;
                if (!request.sellable) return out;
                out.mode = 2;
                break;
        }
    }
    out.pending = out.handled = true;
    if ((out.mode == 1 || out.mode == 2) && request.direct) out.ask = true;
    else out.box = true;
    return out;
}

// A gold item or one FUN_0062e660 refuses: no. Durability under its max
// (signed, so a value above it doesn't count, as Repair All's `!=` does),
// a stack under its max, or a missing charge with the second answer 0:
// yes.
bool repair_gate(const RepairGate& gate) {
    if (gate.gold || !gate.repairable) return false;
    if (gate.has_durability && gate.max_durability != 0 && gate.durability < gate.max_durability) return true;
    if (gate.stackable && gate.quantity < gate.max_quantity) return true;
    return gate.charge_missing && !gate.charge_zero;
}

// FUN_0062a840 reads up to 0x40 of the item's stat 0xcc entries
// (FUN_006261d0): its answer is whether there are any, its second 1
// unless one's charges left (the low byte) are below its most (value
// >> 8).
bool repair_click_gate(const Tables& tables, const d2d::d2s::Item& item) {
    RepairGate gate;
    gate.gold = item.code == "gld";
    gate.repairable = repairable(tables, item);
    gate.has_durability = has_durability(tables, item);
    gate.max_durability = max_durability(item);
    gate.durability = item.durability;
    const auto base = tables.item_base.find(item.code);
    gate.stackable = base != tables.item_base.end() && base->second.stackable;
    gate.quantity = item.quantity;
    gate.max_quantity = stack_most(tables, item);
    gate.charge_missing = std::ranges::any_of(item.props, [](const auto& prop) { return prop.stat == d2d::d2s::kChargedSkill; });
    gate.charge_zero = !charge_missing(item);
    return repair_gate(gate);
}

// FUN_004b82f0 sets it to -1 or to a selectable line; any other index
// halts game.exe (an index past the lines 0x4ed, below -1 0x513, a header
// 0x50f).
bool select_line(int& selected, const std::vector<bool>& selectable, int index) {
    if (index >= int(selectable.size())) return false;
    if (index == -1) { selected = -1; return true; }
    if (index < -1 || !selectable[std::size_t(index)]) return false;
    selected = index;
    return true;
}

}
