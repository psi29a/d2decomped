// SPDX-License-Identifier: GPL-3.0-or-later
// A joined game's "!" over quest NPCs: put on by S->C 0x8a (FUN_004b3380), taken off when an NPC's
// talk opens (S->C 0x28 of another unit than ours, FUN_004b6dd0 -> FUN_004b2390).
#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace d2d::rules {

class NpcAlertBoard {
public:
    // 0x8a at world time_ms: FUN_00470390 first removes the unit's overlay of that row (FUN_0046f0c0),
    // so a repeated packet starts it over.
    void ring(std::uint32_t id, std::uint32_t time_ms) { start_[id] = time_ms; }
    // An NPC's talk opened: every unit whose MonStats npc flag is set loses it (the board holds only NPCs).
    void talk_opened() { start_.clear(); }
    // Level change or new game: the units are gone.
    void reset() { start_.clear(); }
    [[nodiscard]] std::optional<std::uint32_t> start(std::uint32_t id) const {
        const auto found = start_.find(id);
        return found == start_.end() ? std::nullopt : std::optional(found->second);
    }

private:
    std::unordered_map<std::uint32_t, std::uint32_t> start_;
};

}  // namespace d2d::rules
