// SPDX-License-Identifier: GPL-3.0-or-later
// Town critters (docs/research/re/critters.md): game.exe 1.14d FUN_0046c600.
#pragma once

#include "monster_ids.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>

namespace d2d::rules {

inline constexpr int kCritterPlayer = 0, kCritterMonster = 1;   // unit +0x00

// The client's room lists as its critters walk them (FUN_0046c570: room +0x74,
// next +0xe8): a unit goes at its room's head (FUN_0064c2c0) as it first shows
// or changes rooms; `file` gives its room and that stamp, by {kind, id}. The
// walk is room_order over the near list (monsters.hpp). A unit gone from the
// client's view is freed there, so one that comes back is new: `drop_unseen`
// after a frame's filing forgets the ones it didn't file.
struct ClientRooms {
    struct Filed { int room = -2; std::uint32_t stamp = 0, seen = 0; };
    std::map<std::pair<int, int>, Filed> filed;
    std::uint32_t stamps = 0, frame = 0;
    const Filed& file(int kind, int id, int room) { auto& entry = filed[{ kind, id }]; if (entry.room != room) entry = { room, ++stamps }; entry.seen = frame; return entry; }
    void drop_unseen() { std::erase_if(filed, [&](const auto& entry) { return entry.second.seen != frame; }); ++frame; }
};

// FUN_0046c600, FUN_0046c570's callback for a chicken or bunny: does a unit in
// its rooms count for its nearest one? A player, or a monster other than a
// chicken (0x95) or bunny (0x22c). Its dead test FUN_00464820 (0x46c62d) gets
// ECX = the critter, not the candidate, so a corpse counts (bugs.md row 110).
[[nodiscard]] constexpr bool critter_neighbour(bool critter_dead, int unit_type, int row) {
    if (critter_dead) return false;
    if (unit_type == kCritterPlayer) return true;
    return unit_type == kCritterMonster && row != monster_ids::kChicken && row != monster_ids::kBunny;
}

// A bird's flight end (FUN_00648e70(path, 0x5a), kept under 0x4d): its path
// is a line a subtile a step on its longer axis, 77 steps at most, so past
// that it stops at the 77th: the longer axis 77 subtiles on, the other in
// proportion. Cells in and out.
// ponytail: the shorter axis's rounding on that step isn't traced.
[[nodiscard]] inline std::pair<float, float> flight_end(float from_x, float from_y, float to_x, float to_y) {
    const float steps = std::max(std::abs(to_x - from_x), std::abs(to_y - from_y)) * 5, most = 77.f;
    if (steps <= most) return { to_x, to_y };
    return { from_x + (to_x - from_x) * most / steps, from_y + (to_y - from_y) * most / steps };
}

}  // namespace d2d::rules
