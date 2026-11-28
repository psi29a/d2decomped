// SPDX-License-Identifier: GPL-3.0-or-later
// The traced bodies of quests_act3.hpp's functions.
#include "quests_act3.hpp"

#include "quests.hpp"

#include <optional>
#include <string_view>

namespace d2d::rules {

// An item event's direct send (FUN_005b8210 -> FUN_005b81c0). The Will
// changes record +0xb to 7 when it was nonzero, then the repeated prerequisite
// test suppresses its otherwise apparent state-5 / state-6 sends
// (0x5b833a..0x5b8350).
auto KhalimQuest::pickup_log_state(QuestBits const& quest_bits, std::string_view code, const Parts& parts) -> std::optional<int> {
    if (!quest::bit(quest_bits, 0xf, 0) || quest::bit(quest_bits, kQuest, 0)) return std::nullopt;
    if (code == kWillCode) {
        if (log != 0) log = 7;
        return std::nullopt;
    }
    if (code == kFlailCode) {
        if (!parts.eye) return 1;
        if (!parts.brain) return 2;
        if (!parts.heart) return 4;
        return quest::bit(quest_bits, kQuest, 5) ? 7 : 5;
    }
    if (code == kEyeCode) {
        if (!parts.brain) return 2;
        if (!parts.flail) return 3;
        if (!parts.heart) return 4;
        return 7;
    }
    if (code == kHeartCode) {
        if (!parts.brain) return 2;
        if (!parts.flail) return 3;
        if (!parts.eye) return 1;
        return 7;
    }
    if (code == kBrainCode) {
        if (!parts.heart) return 4;
        if (!parts.flail) return 3;
        if (!parts.eye) return 1;
        return 7;
    }
    return std::nullopt;
}

}  // namespace d2d::rules
