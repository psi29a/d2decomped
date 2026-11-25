// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace d2d::rules {

// A monster's sequence position (unit +0x38, 256ths of a row) and the length left to run (+0x48, 256ths of a row).
struct LeapWait { std::uint32_t position = 0, length = 0; };

// FUN_005da120's monster branch with the unit not on the landing yet (its path x, y against the event's): the
// sequence goes back to a row by the unit's MonStats BaseId (FUN_00463860, row +2) and its length grows a row. The
// row (FUN_006212c0 keeps the position's low byte and sets the row): sandleaper1 (0x4e) 8, the ancients (0x21c) 12
// within 2 of the landing (distance: FUN_006417f0, ai_distance) else 10, any other 0. The landing event is the next
// row, so each tick it comes round again and the length left never falls while the unit waits.
constexpr LeapWait leap_wait(int base_id, int distance, std::uint32_t position, std::uint32_t length) {
    const std::uint32_t row = base_id == 0x4e ? 8u : base_id == 0x21c ? (distance < 2 ? 12u : 10u) : 0u;
    return { (position & 0xffu) + row * 0x100u, (length & 0xffffff00u) + 0x100u };
}

}  // namespace d2d::rules
