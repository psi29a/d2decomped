// SPDX-License-Identifier: GPL-3.0-or-later
#include "durability_alert.hpp"

#include "cube.hpp"
#include "item_price.hpp"
#include "rules.hpp"

#include <d2s_items.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace d2d::rules {

namespace {

// stat 0x48 * 100 / max, game.exe's imul and idiv: 32 bits, toward zero.
int durability_percent(int durability, int most) {
    return std::int32_t(std::uint32_t(durability) * 100U) / most;
}

// FUN_00629930 holds, FUN_006289f0 doesn't, and FUN_00625e00 isn't 0.
bool wears(const DurabilitySlot& slot) { return slot.item && slot.has_durability && !slot.stackable && slot.max_durability != 0; }

}  // namespace

// The leaves FUN_00497fe0 asks: FUN_00629930 (rules::has_durability),
// FUN_006289f0 (Items +0x132 stackable), FUN_00629bb0(item, 0x33)
// (ItemTypes row 0x33 is shld; type, then type2), FUN_0062e740 (ItemTypes
// +0xe, Quiver), stat 0x48 and FUN_00625e00 (rules::max_durability).
DurabilitySlot durability_slot(const Tables& tables, const d2d::d2s::Item* item) {
    DurabilitySlot slot;
    if (!item) return slot;
    slot.item = true;
    slot.has_durability = has_durability(tables, *item);
    const auto base = tables.item_base.find(item->code);
    slot.stackable = base != tables.item_base.end() && base->second.stackable;
    slot.shield = item_of_type(tables, item->code, "shld");
    if (const auto info = tables.item_info.find(item->code); info != tables.item_info.end())
        if (const auto kind = tables.types.find(info->second.type); kind != tables.types.end() && !kind->second.quiver.empty()) slot.quiver = 1;
    slot.durability = item->durability;
    slot.max_durability = max_durability(*item);
    return slot;
}

// FUN_00497fe0 (only caller FUN_00456ee0 at 0x456f89, while UI 1, 0xc and
// 0xe are all shut): nothing unless some slot wears() at 20 % or under
// (0x498042: jle 0x14). Then each slot in turn: an empty hand (4, 5)
// draws nothing, any other empty slot draws state 0; an item that doesn't
// pass FUN_00629930 / FUN_006289f0 draws state 0 with quiver and shield
// unasked (0); else state 2 at 0 % or under (0x4980c4), 1 at 20 % or
// under, 0 above; with max 0 it stays 0.
// FUN_00497cf0 (EAX state, ECX quiver, EDX shield, stack slot): frame
// state * 11 + 0x497dd4's jump by slot - 1: head 1, torso 4, belt 2,
// feet 3, gloves 0; right hand 10 with a quiver, else 7 a shield, else 6;
// left hand 9 / 5 / 8. Drawn (FUN_004f6480, colour -1, mode 1) at
// x width - 0x3c (0x71146c), bottom 0x6e; with the automap up
// (FUN_004538d0(10)) at 0x8c, 0xb7 when FUN_0047a560 finds a player
// hostile to us.
std::vector<DurabilityIcon> durability_icons(const std::array<DurabilitySlot, 7>& slots, int screen_width, bool automap, bool hostile) {
    std::vector<DurabilityIcon> icons;
    bool low = false;
    for (const auto& slot : slots)
        if (wears(slot) && durability_percent(slot.durability, slot.max_durability) <= 20) low = true;
    if (!low) return icons;
    const int bottom = automap ? (hostile ? 0x2b : 0) + 0x8c : 0x6e;
    for (std::size_t index = 0; index < slots.size(); ++index) {
        const auto& slot = slots[index];
        const int body = kDurabilitySlots[index];
        if (!slot.item && (body == 4 || body == 5)) continue;
        int state = 0;
        bool quiver = false, shield = false;
        if (slot.item && slot.has_durability && !slot.stackable) {
            quiver = slot.quiver != 0;
            shield = slot.shield;
            if (slot.max_durability != 0) {
                const int percent = durability_percent(slot.durability, slot.max_durability);
                state = percent <= 0 ? 2 : percent <= 20 ? 1 : 0;
            }
        }
        int offset = 0;
        switch (body) {
        case 1: offset = 1; break;
        case 3: offset = 4; break;
        case 4: offset = quiver ? 10 : shield ? 7 : 6; break;
        case 5: offset = quiver ? 9 : shield ? 5 : 8; break;
        case 8: offset = 2; break;
        case 9: offset = 3; break;
        default: break;
        }
        icons.push_back({ state * 11 + offset, screen_width - 0x3c, bottom });
    }
    return icons;
}

}  // namespace d2d::rules
