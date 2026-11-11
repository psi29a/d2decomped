// SPDX-License-Identifier: GPL-3.0-or-later
// ClientSession (session.hpp): the client's unique events (S->C 0x89, the quest event
// table 0x7129d8) and the Horadric Staff's light show they start
// (docs/research/re/quests-act2.md "The orifice"). The rest is in this
// directory.
#include "session.hpp"

#include <log.hpp>
#include <object_ids.hpp>

#include <algorithm>
#include <cstdint>

namespace d2d::client {

// 0x89 {event}: 3, the staff in the orifice (0x46b100), finds the orifice
// (FUN_0065a620: class 0x98, in the player's room; d2d: OperateFn 25) and starts its light
// show there; no orifice, nothing.
auto ClientSession::unique_event(int event, std::uint32_t frame_ms) -> void {
    if (event != 3 || !level) return;
    const auto orifice = std::ranges::find(level->npcs, d2d::rules::operate_fn::kOrifice, &Npc::operate_fn);
    if (orifice == level->npcs.end()) return;
    staff_show = { .playing = true, .x = orifice->x, .y = orifice->y, .born = frame_ms };
    d2d::log::info("The Horadric Staff: its light show starts");
}

}  // namespace d2d::client
