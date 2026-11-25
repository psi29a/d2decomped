// SPDX-License-Identifier: GPL-3.0-or-later
// Another player's faster run / walk on the client, from what the host sent.
#pragma once

#include <d2s_items.hpp>

#include <cstdint>
#include <unordered_map>

namespace d2d::client {

// Stat 96 over the items it wears in the active set (0x9d's own props; slots 0xb / 0xc, the switch, don't count), plus
// `state_frw`, its states' (0xa8 / 0xaa stat lists): what FUN_00623f50 reads on its client unit.
inline int worn_frw(const std::unordered_map<std::uint32_t, d2d::d2s::Item>& worn, int state_frw) {
    constexpr int kSwitchSlot = 0xb;
    int frw = state_frw;
    for (const auto& [item_id, item] : worn)
        if (item.slot < kSwitchSlot)
            for (const auto& prop : item.props) if (prop.stat == d2d::d2s::kFasterMoveVelocity) frw += prop.value;
    return frw;
}

}  // namespace d2d::client
